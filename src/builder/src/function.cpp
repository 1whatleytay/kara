#include <builder/builder.h>

#include <builder/error.h>

#include <parser/scope.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>

FunctionTypename makeFunctionTypenameBase(const FunctionNode *node) {
    std::vector<Typename> parameters(node->parameterCount);

    for (size_t a = 0; a < node->parameterCount; a++) {
        const auto &type = node->children[a]->as<VariableNode>()->fixedType;

        if (!type) {
            throw VerifyError(node->children[a].get(),
                "Function parameter must have given type, default parameters are not implemented.");
        }

        parameters[a] = *type;
    }

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

    for (size_t a = 0; a < node->parameterCount; a++) {
        const auto &parameterType = node->children[a]->as<VariableNode>()->fixedType;

        if (!parameterType.has_value()) {
            throw VerifyError(node->children[a].get(),
                "Function parameter must have given type, default parameters are not implemented.");
        }

        parameterTypes[a] = builder.makeTypename(parameterType.value());
    }

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

    entry.CreateBr(scope.openingBlock);

    if (!scope.currentBlock->getTerminator())
        scope.current.CreateBr(exitBlock);

    if (node->returnType == TypenameNode::nothing)
        exit.CreateRetVoid();
    else
        exit.CreateRet(exit.CreateLoad(returnValue, "final"));
}
