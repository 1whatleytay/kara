#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/search.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>

#include <map>

MatchResult BuilderScope::match(const FunctionNode *node, const std::vector<BuilderResult *> &parameters) {
    auto funcParams = node->parameters();

    if (funcParams.size() != parameters.size())
        return { node, -1, 0 };

    MatchResult result;
    result.node = node;

    BuilderScope testScope(nullptr, *this, false);

    for (size_t a = 0; a < funcParams.size(); a++) {
        const VariableNode *var = funcParams[a];
        const BuilderResult *value = parameters[a];

        assert(var->hasFixedType);
        assert(value->kind != BuilderResult::Kind::Unresolved);

        auto type = function.builder.resolveTypename(var->fixedType());

        if (type != value->type) {
            auto conversion = testScope.convert(*value, type);

            if (!conversion) {
                result.failed = a;
                break;
            }

            result.numImplicit++;
        }
    }

    return result;
}

BuilderResult BuilderScope::call(
    const std::vector<const FunctionNode *> &options, const std::vector<BuilderResult *> &parameters) {

    assert(!options.empty());

    std::vector<MatchResult> checks(options.size());
    std::transform(options.begin(), options.end(), checks.begin(), [this, &parameters](auto o) {
        return match(o, parameters);
    });

    size_t bet = SIZE_T_MAX;
    std::vector<const FunctionNode *> picks;

    for (size_t a = 0; a < checks.size(); a++) {
        const MatchResult &m = checks[a];
        const FunctionNode *f = options[a];

        if (m.failed)
            continue;

        if (m.numImplicit == bet) {
            picks.push_back(f);
        } else if (m.numImplicit < bet) {
            bet = m.numImplicit;
            picks = { f };
        }
    }

    if (picks.empty()) {
        for (const auto &c : checks) {
            assert(c.failed);

            std::string problem;

            if (*c.failed == -1) {
                problem = fmt::format("has {} parameters but call gave {}.",
                    c.node->parameterCount, parameters.size());
            } else {
                problem = fmt::format("has parameter #{} of type {} but was given type {}.", *c.failed + 1,
                    toString(function.builder.resolveTypename(c.node->parameters()[*c.failed]->fixedType())),
                    toString(parameters[*c.failed]->type));
            }

            if (c.node->state.text.empty()) {
                fmt::print("Function {} (from generated AST) {}\n",
                    c.node->name, problem);
            } else {
                fmt::print("Function {} (from line {}) {}\n",
                    c.node->name, LineDetails(c.node->state.text, c.node->index).lineNumber, problem);
            }
        }

        throw std::runtime_error("No functions match given function parameters.");
    }

    if (picks.size() != 1 && !(std::all_of(picks.begin(), picks.end(), [](auto f) { return f->isExtern; })/* && bet == 0*/))
        throw std::runtime_error(fmt::format("Multiple functions match the most accurate conversion level, {}.", bet));

    const FunctionNode *pick = picks.front();
    auto pickVariables = pick->parameters();

    auto builderFunction = function.builder.makeFunction(pick);

    std::vector<Value *> passParameters(parameters.size());

    for (size_t a = 0; a < parameters.size(); a++) {
        assert(pickVariables[a]->hasFixedType);

        passParameters[a] = get(convert(
            *parameters[a], function.builder.resolveTypename(pickVariables[a]->fixedType())).value());
    }

    return BuilderResult(
        BuilderResult::Kind::Raw,
        current ? current->CreateCall(builderFunction->function, passParameters) : nullptr,
        *builderFunction->type.returnType
    );
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
