#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/assign.h>
#include <parser/function.h>
#include <parser/search.h>
#include <parser/statement.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <map>

namespace kara::builder {
    MatchResult Scope::match(const std::vector<const parser::Variable *> &parameters, const MatchInput &input) {
        if (parameters.size() != input.parameters.size()) {
            return { fmt::format("Expected {} parameters but got {}.", parameters.size(), input.parameters.size()), {},
                0 };
        }

        MatchResult result;
        result.map.resize(parameters.size());

        std::vector<bool> taken(input.parameters.size());

        builder::Scope testScope(nullptr, *this, false);

        auto tryMove = [&](size_t from, size_t to) {
            const parser::Variable *var = parameters[to];
            const builder::Result *value = input.parameters[from];

            if (taken[from])
                throw;

            if (result.map[to]) {
                result.failed = fmt::format("Parameter at index {} with name {} is passed twice.", to, var->name);
                return;
            }

            assert(var->hasFixedType);

            auto type = builder.resolveTypename(var->fixedType());

            if (type != value->type) {
                auto conversion = testScope.convert(*value, type);

                if (!conversion) {
                    result.failed = fmt::format("Cannot convert parameter {} of type {} to type {}.", from,
                        toString(value->type), toString(type));

                    return;
                }

                result.numImplicit++;
            }

            taken[from] = true;
            result.map[to] = value;
        };

        for (const auto &pair : input.names) {
            auto iterator = std::find_if(
                parameters.begin(), parameters.end(), [&pair](auto v) { return v->name == pair.second; });

            if (iterator == parameters.end()) {
                result.failed = fmt::format("Expected parameter named {}, but none found.", pair.second);
                return result;
            }

            auto index = std::distance(parameters.begin(), iterator);

            tryMove(pair.first, index);
            if (result.failed)
                return result;
        }

        size_t funcIndex = 0;
        size_t parameterIndex = 0;

        while (funcIndex < taken.size() && parameterIndex < taken.size()) {
            while (parameterIndex < taken.size() && taken[parameterIndex])
                parameterIndex++;

            while (funcIndex < result.map.size() && result.map[funcIndex])
                funcIndex++;

            if (parameterIndex >= taken.size() && funcIndex >= result.map.size())
                break;

            tryMove(parameterIndex, funcIndex);
            if (result.failed)
                return result;
        }

        if (std::any_of(taken.begin(), taken.end(), std::logical_not())
            || std::any_of(result.map.begin(), result.map.end(), std::logical_not()))
            throw;

        return result;
    }

    std::variant<builder::Result, MatchCallError> Scope::call(
        const std::vector<const hermes::Node *> &options, const MatchInput &input, llvm::IRBuilder<> *irBuilder) {

        assert(!options.empty());

        using TestResult = std::tuple<const hermes::Node *, MatchResult>;

        std::vector<TestResult> checks(options.size());
        std::transform(options.begin(), options.end(), checks.begin(), [this, &input](const hermes::Node *o) {
            switch (o->is<parser::Kind>()) {
            case parser::Kind::Function:
                return std::make_tuple(o, match(o->as<parser::Function>()->parameters(), input));
            case parser::Kind::Type:
                return std::make_tuple(o, match(o->as<parser::Type>()->fields(), input));
            default:
                throw;
            }
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

            return MatchCallError { "No functions match given function parameters.", std::move(errors) };
        }

        auto isExternMatch = [](auto f) {
            const hermes::Node *node = std::get<0>(*f);

            return node->is(parser::Kind::Function) && node->as<parser::Function>()->isExtern;
        };

        if (picks.size() != 1 && !(std::all_of(picks.begin(), picks.end(), isExternMatch) /* && bet == 0*/)) {
            return MatchCallError { fmt::format(
                "Multiple functions match the most accurate conversion level, {}.", bet) };
        }

        auto [pick, match] = *picks.front();

        switch (pick->is<parser::Kind>()) {
        case parser::Kind::Function: {
            auto e = pick->as<parser::Function>();

            auto pickVariables = e->parameters();

            auto builderFunction = builder.makeFunction(e);

            std::vector<llvm::Value *> passParameters(match.map.size());

            for (size_t a = 0; a < passParameters.size(); a++) {
                assert(pickVariables[a]->hasFixedType);

                auto type = builder.resolveTypename(pickVariables[a]->fixedType());

                passParameters[a] = get(convert(*match.map[a], type).value());
            }

            return builder::Result(builder::Result::FlagTemporary,
                irBuilder ? irBuilder->CreateCall(builderFunction->function, passParameters) : nullptr,
                *builderFunction->type.returnType, &statementContext);
        }

        case parser::Kind::Type: {
            auto e = pick->as<parser::Type>();
            auto fields = e->fields();

            utils::NamedTypename type = { e->name, e };

            if (irBuilder) {
                assert(function);

                builder::Type *builderType = builder.makeType(e);
                llvm::Value *value = function->entry.CreateAlloca(builderType->type);

                assert(fields.size() == match.map.size()); // sanity

                for (size_t a = 0; a < fields.size(); a++) {
                    const parser::Variable *field = fields[a];
                    const builder::Result *m = match.map[a];

                    assert(field->hasFixedType);

                    irBuilder->CreateStore(get(convert(*m, builder.resolveTypename(field->fixedType())).value()),
                        irBuilder->CreateStructGEP(value, builderType->indices[field]));
                }

                return builder::Result(
                    builder::Result::FlagReference | builder::Result::FlagTemporary, value, type, &statementContext);
            } else {
                return builder::Result(
                    builder::Result::FlagReference | builder::Result::FlagTemporary, nullptr, type, &statementContext);
            }
        }

        default:
            throw;
        }
    }

    std::variant<builder::Result, MatchCallError> Scope::call(
        const std::vector<const hermes::Node *> &options, const MatchInput &input) {

        return call(options, input, current ? &(*current) : nullptr);
    }

    builder::Result Scope::callUnpack(
        const std::variant<builder::Result, MatchCallError> &result, const hermes::Node *node) {
        struct {
            const hermes::Node *from;

            builder::Result operator()(const builder::Result &result) const { return result; }

            builder::Result operator()(const MatchCallError &error) const {
                throw VerifyError(from, "{}\n{}", fmt::format("{}", fmt::join(error.messages, "\n")), error.problem);
            }
        } visitor { node };

        return std::visit(visitor, result);
    }

    void Function::build() {
        bool responsible = parser::search::exclusive::root(node) == builder.root;

        utils::Typename returnTypename = utils::PrimitiveTypename::from(utils::PrimitiveType::Nothing);

        const hermes::Node *body = nullptr;

        switch (node->is<parser::Kind>()) {
        case parser::Kind::Function: {
            auto e = node->as<parser::Function>();

            if (auto fixed = e->fixedType())
                returnTypename = builder.resolveTypename(fixed);

            body = responsible ? e->body() : nullptr;
            // Check for inferred type from expression node maybe?
            if (body && body->is(parser::Kind::Expression) && !e->hasFixedType
                && returnTypename == utils::PrimitiveTypename::from(utils::PrimitiveType::Nothing)) {

                builder::Scope productScope(body, *this, false);

                returnTypename = productScope.product.value().type;
            }

            returnType = builder.makeTypename(returnTypename);

            std::vector<utils::Typename> parameters(e->parameterCount);
            std::vector<llvm::Type *> parameterTypes(e->parameterCount);

            auto parameterVariables = e->parameters();

            for (size_t a = 0; a < e->parameterCount; a++) {
                auto fixed = parameterVariables[a]->fixedType();

                if (!fixed) {
                    throw VerifyError(e->children[a].get(),
                        "Function parameter must have given type, default "
                        "parameters are not implemented.");
                }

                parameters[a] = builder.resolveTypename(fixed);
                parameterTypes[a] = builder.makeTypename(parameters[a]);
            }

            type = { utils::FunctionTypename::Kind::Pointer, std::move(parameters),
                std::make_shared<utils::Typename>(returnTypename) };

            llvm::FunctionType *valueType = llvm::FunctionType::get(returnType, parameterTypes, false);
            function = llvm::Function::Create(
                valueType, llvm::GlobalVariable::ExternalLinkage, 0, e->name, builder.module.get());

            break;
        }

        case parser::Kind::Type: {
            assert(purpose == Purpose::TypeDestructor); // no weirdness please

            auto e = node->as<parser::Type>();
            auto structType = builder.makeType(e);

            auto voidType = llvm::Type::getVoidTy(builder.context);
            auto paramType = llvm::PointerType::get(structType->type, 0);

            auto name = fmt::format("{}_implicit_dest", e->name);

            llvm::FunctionType *valueType = llvm::FunctionType::get(voidType, { paramType }, false);
            function = llvm::Function::Create(
                valueType, llvm::GlobalVariable::ExternalLinkage, 0, name, builder.module.get());

            body = responsible ? e : nullptr;

            break;
        }

        default:
            throw;
        }

        if (body) {
            entryBlock = llvm::BasicBlock::Create(builder.context, "entry", function);
            exitBlock = llvm::BasicBlock::Create(builder.context, "exit", function);

            entry.SetInsertPoint(entryBlock);
            exit.SetInsertPoint(exitBlock);

            if (returnTypename != utils::PrimitiveTypename::from(utils::PrimitiveType::Nothing))
                returnValue = entry.CreateAlloca(returnType, nullptr, "result");

            builder::Scope scope(body, *this);

            if (body->is(parser::Kind::Expression)) {
                if (!scope.product.has_value())
                    throw VerifyError(body, "Missing product for expression type function.");

                builder::Result result = scope.product.value();

                std::optional<builder::Result> resultConverted = scope.convert(result, *type.returnType);

                if (!resultConverted.has_value()) {
                    throw VerifyError(body, "Method returns type {} but expression is of type {}.",
                        toString(*type.returnType), toString(result.type));
                }

                result = resultConverted.value();

                scope.current.value().CreateStore(scope.get(result), returnValue);
            }

            entry.CreateBr(scope.openingBlock);

            if (body->is(parser::Kind::Code)) { // wwww
                scope.destinations[Scope::ExitPoint::Regular] = exitBlock;
                scope.destinations[Scope::ExitPoint::Return] = exitBlock;
                scope.commit();
            } else {
                scope.current->CreateBr(exitBlock);
            }

            if (returnTypename == utils::PrimitiveTypename::from(utils::PrimitiveType::Nothing))
                exit.CreateRetVoid();
            else
                exit.CreateRet(exit.CreateLoad(returnValue, "final"));
        }
    }

    Function::Function(const hermes::Node *node, Builder &builder)
        : builder(builder)
        , node(node)
        , entry(builder.context)
        , exit(builder.context) {
        purpose = ([node]() {
            switch (node->is<parser::Kind>()) {
            case parser::Kind::Function:
                return Purpose::UserFunction;
            case parser::Kind::Type:
                return Purpose::TypeDestructor;
            default:
                throw;
            }
        })();
    }
}
