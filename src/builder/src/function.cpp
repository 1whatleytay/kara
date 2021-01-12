#include <builder/builder.h>

#include <builder/lifetime.h>

#include <parser/scope.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>

FunctionTypename makeFunctionTypenameBase(const FunctionNode *node) {
    std::vector<Typename> parameters(node->parameterCount);

    for (size_t a = 0; a < node->parameterCount; a++)
        parameters[a] = node->children[a]->as<VariableNode>()->type;

    return {
        FunctionTypename::Kind::Pointer,
        std::make_shared<Typename>(node->returnType),
        std::move(parameters)
    };
}

BuilderFunction::BuilderFunction(const FunctionNode *node, Builder &builder)
    : builder(builder), node(node), entry(builder.context), exit(builder.context) {
    Type *returnType = builder.makeTypename(node->returnType);
    std::vector<Type *> parameterTypes(node->parameterCount);

    for (size_t a = 0; a < node->parameterCount; a++)
        parameterTypes[a] = builder.makeTypename(node->children[a]->as<VariableNode>()->type);

    FunctionType *valueType = FunctionType::get(returnType, parameterTypes, false);

    type = makeFunctionTypenameBase(node);
    function = Function::Create(
        valueType, GlobalVariable::ExternalLinkage, 0, node->name, &builder.module);

    entryBlock = BasicBlock::Create(builder.context, "entry", function);
    exitBlock = BasicBlock::Create(builder.context, "exit", function);

    entry.SetInsertPoint(entryBlock);
    exit.SetInsertPoint(exitBlock);

    if (node->returnType != TypenameNode::nothing)
        returnValue = entry.CreateAlloca(returnType, nullptr, "result");

    BuilderScope scope(node->children[node->parameterCount]->as<CodeNode>(), *this);

    // Publish ending information about lifetimes.
    for (size_t a = 0; a < node->parameterCount; a++) {
        auto *varNode = node->children[a]->as<VariableNode>();

        const Typename &varType = varNode->type;

        if (!std::holds_alternative<ReferenceTypename>(varType))
            continue;

        std::shared_ptr<MultipleLifetime> initial = std::make_shared<MultipleLifetime>();
        initial->push_back(makeAnonymousLifetime(varType, varNode));

        const auto &scopeLifetime = scope.lifetimes[varNode];

        // If the transform is redundant, drop it.
        const auto &final = initial->compare(*scopeLifetime) ? nullptr : scopeLifetime;

        type.transforms[a] = LifetimeTransform {
            std::move(initial),
            final
        };
    }

    entry.CreateBr(scope.openingBlock);

    if (!scope.currentBlock->getTerminator())
        scope.current.CreateBr(exitBlock);

    if (node->returnType == TypenameNode::nothing)
        exit.CreateRetVoid();
    else
        exit.CreateRet(exit.CreateLoad(returnValue, "final"));
}
