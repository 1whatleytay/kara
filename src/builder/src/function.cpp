#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/type.h>
#include <parser/search.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>

#include <map>

MatchResult BuilderScope::match(const std::vector<const VariableNode *> &parameters, const MatchInput &input) {
    if (parameters.size() != input.parameters.size()) {
        return {
            fmt::format("Expected {} parameters but got {}.", parameters.size(), input.parameters.size()),
            { }, 0
        };
    }

    MatchResult result;
    result.map.resize(parameters.size());

    std::vector<bool> taken(input.parameters.size());

    BuilderScope testScope(nullptr, *this, false);

    auto tryMove = [&](size_t from, size_t to) {
        const VariableNode *var = parameters[to];
        const BuilderResult *value = input.parameters[from];

        if (taken[from])
            throw;

        if (result.map[to]) {
            result.failed = fmt::format("Parameter at index {} with name {} is passed twice.", to, var->name);
            return;
        }

        assert(var->hasFixedType);
        assert(value->kind != BuilderResult::Kind::Unresolved);

        auto type = function.builder.resolveTypename(var->fixedType());

        if (type != value->type) {
            auto conversion = testScope.convert(*value, type);

            if (!conversion) {
                result.failed = fmt::format(
                    "Cannot convert parameter {} of type {} to type {}.",
                    from, toString(value->type), toString(type));

                return;
            }

            result.numImplicit++;
        }

        taken[from] = true;
        result.map[to] = value;
    };

    for (const auto &pair : input.names) {
        auto iterator = std::find_if(parameters.begin(), parameters.end(), [&pair](auto v) {
            return v->name == pair.second;
        });

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

std::variant<BuilderResult, MatchCallError> BuilderScope::call(
    const std::vector<const Node *> &options, const MatchInput &input, IRBuilder<> *builder) {

    assert(!options.empty());

    using TestResult = std::tuple<const Node *, MatchResult>;

    std::vector<TestResult> checks(options.size());
    std::transform(options.begin(), options.end(), checks.begin(), [this, &input](const Node *o) {
        switch (o->is<Kind>()) {
            case Kind::Function:
                return std::make_tuple(o, match(o->as<FunctionNode>()->parameters(), input));
            case Kind::Type:
                return std::make_tuple(o, match(o->as<TypeNode>()->fields(), input));
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

            switch (node->is<Kind>()) {
                case Kind::Type: {
                    auto e = node->as<TypeNode>();

                    if (node->state.text.empty()) {
                        errors.push_back(fmt::format("Type {} (from generated AST) {}\n", e->name, problem));
                    } else {
                        auto line = LineDetails(node->state.text, node->index).lineNumber;
                        errors.push_back(fmt::format("Type {} (from line {}) {}\n", e->name, line, problem));
                    }

                    break;
                }
                case Kind::Function: {
                    auto e = node->as<FunctionNode>();

                    if (node->state.text.empty()) {
                        errors.push_back(fmt::format("Function {} (from generated AST) {}\n", e->name, problem));
                    } else {
                        auto line = LineDetails(node->state.text, node->index).lineNumber;
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
        const Node *node = std::get<0>(*f);

        return node->is(Kind::Function) && node->as<FunctionNode>()->isExtern;
    };

    if (picks.size() != 1 && !(std::all_of(picks.begin(), picks.end(), isExternMatch)/* && bet == 0*/))
        return MatchCallError { fmt::format("Multiple functions match the most accurate conversion level, {}.", bet) };

    auto [pick, match] = *picks.front();

    switch (pick->is<Kind>()) {
        case Kind::Function: {
            auto e = pick->as<FunctionNode>();

            auto pickVariables = e->parameters();

            auto builderFunction = function.builder.makeFunction(e);

            std::vector<Value *> passParameters(match.map.size());

            for (size_t a = 0; a < passParameters.size(); a++) {
                assert(pickVariables[a]->hasFixedType);

                auto type = function.builder.resolveTypename(pickVariables[a]->fixedType());

                passParameters[a] = get(convert(*match.map[a], type).value());
            }

            return BuilderResult(
                BuilderResult::Kind::Raw,
                builder ? builder->CreateCall(builderFunction->function, passParameters) : nullptr,
                *builderFunction->type.returnType,
                &statementContext
            );
        }

        case Kind::Type: {
            auto e = pick->as<TypeNode>();
            auto fields = e->fields();

            NamedTypename type = { e };

            if (builder) {
                BuilderType *builderType = function.builder.makeType(e);
                Value *value = function.entry.CreateAlloca(builderType->type);

                assert(fields.size() == match.map.size()); // sanity

                for (size_t a = 0; a < fields.size(); a++) {
                    const VariableNode *field = fields[a];
                    const BuilderResult *m = match.map[a];

                    assert(field->hasFixedType);

                    builder->CreateStore(
                        get(convert(*m, function.builder.resolveTypename(field->fixedType())).value()),
                        builder->CreateStructGEP(value, builderType->indices[field]));
                }

                return BuilderResult(
                    BuilderResult::Kind::Literal,
                    value,
                    type,
                    &statementContext
                );
            } else {
                return BuilderResult(
                    BuilderResult::Kind::Literal,
                    nullptr,
                    type,
                    &statementContext
                );
            }
        }

        default:
            throw;
    }
}

std::variant<BuilderResult, MatchCallError> BuilderScope::call(
    const std::vector<const Node *> &options, const MatchInput &input) {

    return call(options, input, current ? &(*current) : nullptr);
}

BuilderResult BuilderScope::callUnpack(const std::variant<BuilderResult, MatchCallError> &result, const Node *node) {
    struct {
        const Node *from;

        BuilderResult operator()(const BuilderResult &result) const {
            return result;
        }

        BuilderResult operator()(const MatchCallError &error) const {
            throw VerifyError(from, "{}\n{}",
                fmt::format("{}", fmt::join(error.messages, "\n")), error.problem);
        }
    } visitor { node };

    return std::visit(visitor, result);
}

void BuilderFunction::build() {
    bool responsible = search::exclusive::parents(node, [this](const Node *n) {
        return n == builder.file.root.get();
    });

    Typename returnTypename = PrimitiveTypename::from(PrimitiveType::Nothing);

    if (auto fixed = node->fixedType())
        returnTypename = builder.resolveTypename(fixed);

    const Node *body = responsible ? node->body() : nullptr;
    // Check for inferred type from expression node maybe?
    if (body && body->is(Kind::Expression) && !node->hasFixedType
        && returnTypename == PrimitiveTypename::from(PrimitiveType::Nothing)) {

        returnTypename = BuilderScope(body, *this, false).product.value().type;
    }

    returnType = builder.makeTypename(returnTypename);

    std::vector<Typename> parameters(node->parameterCount);
    std::vector<Type *> parameterTypes(node->parameterCount);

    auto parameterVariables = node->parameters();

    for (size_t a = 0; a < node->parameterCount; a++) {
        auto fixed = parameterVariables[a]->fixedType();

        if (!fixed) {
            throw VerifyError(node->children[a].get(),
                "Function parameter must have given type, default parameters are not implemented.");
        }

        parameters[a] = builder.resolveTypename(fixed);
        parameterTypes[a] = builder.makeTypename(parameters[a]);
    }

    type = {
        FunctionTypename::Kind::Pointer,
        std::make_shared<Typename>(returnTypename),
        std::move(parameters)
    };

    FunctionType *valueType = FunctionType::get(returnType, parameterTypes, false);
    function = Function::Create(valueType, GlobalVariable::ExternalLinkage, 0, node->name, builder.module.get());

    if (body) {
        entryBlock = BasicBlock::Create(builder.context, "entry", function);
        exitBlock = BasicBlock::Create(builder.context, "exit", function);

        entry.SetInsertPoint(entryBlock);
        exit.SetInsertPoint(exitBlock);

        if (returnTypename != PrimitiveTypename::from(PrimitiveType::Nothing))
            returnValue = entry.CreateAlloca(returnType, nullptr, "result");

        BuilderScope scope(body, *this);

        if (body->is(Kind::Expression)) {
            if (!scope.product.has_value())
                throw VerifyError(body, "Missing product for expression type function.");

            BuilderResult result = scope.product.value();

            std::optional<BuilderResult> resultConverted = scope.convert(result, *type.returnType);

            if (!resultConverted.has_value()) {
                throw VerifyError(body,
                    "Method returns type {} but expression is of type {}.",
                    toString(*type.returnType), toString(result.type));
            }

            result = resultConverted.value();

            scope.current.value().CreateStore(scope.get(result), returnValue);
        }

        entry.CreateBr(scope.openingBlock);

        scope.destinations[BuilderScope::ExitPoint::Regular] = exitBlock;
        scope.destinations[BuilderScope::ExitPoint::Return] = exitBlock;
        scope.commit();

//        if (!scope.currentBlock->getTerminator())
//            scope.current.value().CreateBr(exitBlock);

        if (returnTypename == PrimitiveTypename::from(PrimitiveType::Nothing))
            exit.CreateRetVoid();
        else
            exit.CreateRet(exit.CreateLoad(returnValue, "final"));
    }
}

BuilderFunction::BuilderFunction(const FunctionNode *node, Builder &builder)
    : builder(builder), node(node), entry(builder.context), exit(builder.context) { }
