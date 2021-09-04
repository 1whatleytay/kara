#include <builder/operations.h>

#include <parser/function.h>
#include <parser/type.h>
#include <parser/variable.h>

namespace kara::builder::ops::matching {
    MatchResult match(
        Builder &builder, const std::vector<const parser::Variable *> &parameters, const MatchInput &input) {
        if (parameters.size() != input.parameters.size()) {
            auto error = fmt::format("Expected {} parameters but got {}.", parameters.size(), input.parameters.size());

            return { error, {}, 0 };
        }

        MatchResult result;
        result.map.reserve(parameters.size());
        std::vector<std::optional<builder::Result>> map(parameters.size());

        std::vector<bool> taken(input.parameters.size());

        ops::Context context = { builder };

        auto tryMove = [&](size_t from, size_t to) -> bool {
            const parser::Variable *var = parameters[to];
            const builder::Result &value = input.parameters[from];

            if (taken[from])
                throw;

            if (map[to]) {
                result.failed = fmt::format("Parameter at index {} with name {} is passed twice.", to, var->name);
                return false;
            }

            assert(var->hasFixedType);

            auto type = builder.resolveTypename(var->fixedType());

            if (type != value.type) {
                auto conversion = ops::makeConvert(context, value, type);

                if (!conversion) {
                    result.failed = fmt::format("Cannot convert parameter {} of type {} to type {}.", from,
                        toString(value.type), toString(type));

                    return false;
                }

                result.numImplicit++;
            }

            taken[from] = true;
            map[to] = value;

            return true;
        };

        for (const auto &pair : input.names) {
            auto iterator = std::find_if(
                parameters.begin(), parameters.end(), [&pair](auto v) { return v->name == pair.second; });

            if (iterator == parameters.end()) {
                result.failed = fmt::format("Expected parameter named {}, but none found.", pair.second);
                return result;
            }

            auto index = std::distance(parameters.begin(), iterator);

            if (!tryMove(pair.first, index))
                return result;
        }

        size_t funcIndex = 0;
        size_t parameterIndex = 0;

        while (funcIndex < taken.size() && parameterIndex < taken.size()) {
            while (parameterIndex < taken.size() && taken[parameterIndex])
                parameterIndex++;

            while (funcIndex < map.size() && map[funcIndex])
                funcIndex++;

            if (parameterIndex >= taken.size() && funcIndex >= map.size())
                break;

            if (!tryMove(parameterIndex, funcIndex))
                return result;
        }

        std::transform(
            map.begin(), map.end(), std::back_inserter(result.map), [](const auto &maybe) { return maybe.value(); });

        return result;
    }

    CallWrapped call(
        const Context &context, const std::vector<const hermes::Node *> &options, const MatchInput &input) {

        assert(!options.empty());

        using TestResult = std::tuple<const hermes::Node *, MatchResult>;

        auto paramsFromNode = [](const hermes::Node *node) {
            switch (node->is<parser::Kind>()) {
            case parser::Kind::Function:
                return node->as<parser::Function>()->parameters();
            case parser::Kind::Type:
                return node->as<parser::Type>()->fields();
            default:
                throw;
            }
        };

        std::vector<TestResult> checks(options.size());
        std::transform(options.begin(), options.end(), checks.begin(), [&](const hermes::Node *o) {
            return std::make_tuple(o, ops::matching::match(context.builder, paramsFromNode(o), input));
        });

        size_t bet = SIZE_T_MAX;
        std::vector<const TestResult *> picks;

        for (const TestResult &check : checks) {
            const auto &[node, result] = check;

            if (result.failed)
                continue;

            if (result.numImplicit == bet) {
                picks.emplace_back(&check);
            } else if (result.numImplicit < bet) {
                bet = result.numImplicit;
                picks.clear();
                picks.emplace_back(&check);
            }
        }

        if (picks.empty()) {
            std::vector<std::string> errors;

            for (const auto &check : checks) {
                const auto &[node, result] = check;

                assert(result.failed);

                std::string problem = *result.failed;

                switch (node->is<parser::Kind>()) {
                case parser::Kind::Type: {
                    auto e = node->as<parser::Type>();

                    if (node->state.text.empty()) {
                        errors.push_back(fmt::format("Type {} (from generated AST) {}\n", e->name, problem));
                    } else {
                        auto line = hermes::LineDetails(node->state.text, node->index).lineNumber;
                        errors.push_back(fmt::format("Type {} (from line {}) {}\n", e->name, line, problem));
                    }

                    break;
                }
                case parser::Kind::Function: {
                    auto e = node->as<parser::Function>();

                    if (node->state.text.empty()) {
                        errors.push_back(fmt::format("Function {} (from generated AST) {}\n", e->name, problem));
                    } else {
                        auto line = hermes::LineDetails(node->state.text, node->index).lineNumber;
                        errors.push_back(fmt::format("Function {} (from line {}) {}\n", e->name, line, problem));
                    }

                    break;
                }

                default:
                    throw;
                }
            }

            return CallError {
                "No functions match given function parameters.",
                std::move(errors),
            };
        }

        auto isExternMatch = [](auto f) {
            const hermes::Node *node = std::get<0>(*f);

            return node->is(parser::Kind::Function) && node->as<parser::Function>()->isExtern;
        };

        if (picks.size() != 1 && !(std::all_of(picks.begin(), picks.end(), isExternMatch) /* && bet == 0*/)) {
            return CallError {
                fmt::format("Multiple functions match the most accurate conversion level, {}.", bet),
            };
        }

        auto [pick, match] = *picks.front();

        switch (pick->is<parser::Kind>()) {
        case parser::Kind::Function: {
            auto e = pick->as<parser::Function>();

            auto pickVariables = e->parameters();

            auto builderFunction = context.builder.makeFunction(e);

            std::vector<llvm::Value *> passParameters(match.map.size());

            for (size_t a = 0; a < passParameters.size(); a++) {
                assert(pickVariables[a]->hasFixedType);

                auto type = context.builder.resolveTypename(pickVariables[a]->fixedType());

                passParameters[a] = ops::get(context, ops::makeConvert(context, match.map[a], type).value());
            }

            return builder::Result {
                builder::Result::FlagTemporary,
                context.ir ? context.ir->CreateCall(builderFunction->function, passParameters) : nullptr,
                *builderFunction->type.returnType,
                context.accumulator,
            };
        }

        case parser::Kind::Type: {
            auto e = pick->as<parser::Type>();
            auto fields = e->fields();

            utils::NamedTypename type = { e->name, e };

            llvm::Value *value = nullptr;

            if (context.ir) {
                assert(context.function);

                builder::Type *builderType = context.builder.makeType(e);
                value = context.function->entry.CreateAlloca(builderType->type);

                assert(fields.size() == match.map.size()); // sanity

                for (size_t a = 0; a < fields.size(); a++) {
                    const parser::Variable *field = fields[a];
                    const builder::Result &m = match.map[a];

                    assert(field->hasFixedType);

                    auto fieldType = context.builder.resolveTypename(field->fixedType());
                    auto llvmValue = ops::get(context, ops::makeConvert(context, m, fieldType).value());
                    auto llvmPtr = context.ir->CreateStructGEP(value, builderType->indices[field]);

                    context.ir->CreateStore(llvmValue, llvmPtr);
                }
            }

            return builder::Result {
                builder::Result::FlagReference | builder::Result::FlagTemporary,
                value,
                type,
                context.accumulator,
            };
        }

        default:
            throw;
        }
    }

    builder::Result unwrap(const CallWrapped &result, const hermes::Node *node) {
        struct {
            const hermes::Node *from;

            builder::Result operator()(const builder::Result &result) const { return result; }

            builder::Result operator()(const CallError &error) const {
                throw VerifyError(from, "{}\n{}", fmt::format("{}", fmt::join(error.messages, "\n")), error.problem);
            }
        } visitor { node };

        return std::visit(visitor, result);
    }
}