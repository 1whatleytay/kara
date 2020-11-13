#include <builder/builder.h>

#include <parser/code.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>

FunctionTypename Builder::makeFunctionTypenameBase(const FunctionNode *node) {
    std::vector<Typename> parameters(node->parameterCount);

    for (size_t a = 0; a < node->parameterCount; a++)
        parameters[a] = node->children[a]->as<VariableNode>()->type;

    return {
        FunctionTypename::Kind::Pointer,
        std::make_shared<Typename>(node->returnType),
        std::move(parameters)
    };
}

Callable Builder::makeFunction(const FunctionNode *node) {
    auto cache = functions.find(node);
    if (cache != functions.end())
        return cache->second;

    Type *returnType = makeTypename(node->returnType);
    std::vector<Type *> parameterTypes(node->parameterCount);

    for (size_t a = 0; a < node->parameterCount; a++)
        parameterTypes[a] = makeTypename(node->children[a]->as<VariableNode>()->type);

    FunctionType *type = FunctionType::get(returnType, parameterTypes, false);

    // Please manage my memory for me...
    Function *function = Function::Create(
        type, GlobalVariable::ExternalLinkage, 0, node->name, &module);
    Callable callable = { std::make_shared<FunctionTypename>(makeFunctionTypenameBase(node)), function };

    functions[node] = callable;

    Scope scope;

    for (size_t a = 0; a < node->parameterCount; a++) {
        makeParameterVariable(node->children[a]->as<VariableNode>(), function->getArg(a), scope);
    }

    scope.entry = BasicBlock::Create(context, "entry", function);
    scope.exit = BasicBlock::Create(context, "exit", function);

    BasicBlock *original = BasicBlock::Create(context, "", function, scope.exit);
    scope.current = original;

    if (node->returnType != TypenameNode::nothing) {
        scope.returnType = node->returnType;
        scope.returnValue = IRBuilder<>(scope.entry).CreateAlloca(returnType, nullptr, "result");
    }

    makeCode(node->children[node->parameterCount]->as<CodeNode>(), scope);

    IRBuilder<>(scope.entry).CreateBr(original);

    if (!scope.current->getTerminator())
        IRBuilder<>(scope.current).CreateBr(scope.exit);

    {
        IRBuilder<> exit(scope.exit);

        if (node->returnType == TypenameNode::nothing)
            exit.CreateRetVoid();
        else
            exit.CreateRet(exit.CreateLoad(scope.returnValue, "final"));
    }

    return callable;
}
