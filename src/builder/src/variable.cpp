#include <builder/builder.h>

#include <parser/variable.h>

#include <builder/search.h>

Variable Builder::makeLocalVariable(const VariableNode *node, Scope &scope) {
    IRBuilder<> builder(scope.entry);

    Value *value = builder.CreateAlloca(makeTypename(node->type), nullptr, node->name);
    Variable result = { node, node->type, value, true };
    variables[node] = result;

    return result;
}

Variable Builder::makeParameterVariable(const VariableNode *node, Value *value, Scope &scope) {
    Variable result = {
        node,
        node->type,
        value,
        false
    };
    variables[node] = result;

    return result;
}

Variable Builder::makeVariable(const VariableNode *node, const Scope &scope) {
    auto cache = variables.find(node);
    if (cache != variables.end())
        return cache->second;

    // Make variable shouldn't create the variable if it has not been generated already.
//    bool isLocal = search::exclusive::parents(node, [](const Node *node) {
//        return node->is(Kind::Function);
//    });
//
//    if (isLocal)
//        return makeLocalVariable(node, scope);

    throw std::runtime_error("Cannot match variable.");
}
