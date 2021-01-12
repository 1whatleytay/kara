#include <builder/builder.h>

#include <parser/variable.h>

#include <fmt/format.h>

std::shared_ptr<MultipleLifetime> BuilderVariable::makeExpressionLifetime() const {
    auto base = std::make_shared<MultipleLifetime>();
    auto child = std::make_shared<MultipleLifetime>();

    base->push_back(std::make_shared<ReferenceLifetime>(child));
    child->push_back(std::make_shared<VariableLifetime>(node));

    return base;
}

BuilderVariable::BuilderVariable(const VariableNode *node, BuilderScope &scope)
    : function(scope.function), node(node) {
    value = function.entry.CreateAlloca(
        function.builder.makeTypename(node->type), nullptr, node->name);

    auto scopeLifetime = std::make_shared<MultipleLifetime>();
    std::shared_ptr<Lifetime> defaultLifetime = makeDefaultLifetime(node->type, node);
    if (defaultLifetime)
        scopeLifetime->push_back(std::move(defaultLifetime));

    scope.lifetimes[node] = std::move(scopeLifetime);

    lifetime = makeExpressionLifetime();
}

BuilderVariable::BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope)
    : function(scope.function), node(node) {
    value = function.entry.CreateAlloca(
        function.builder.makeTypename(node->type), nullptr, fmt::format("{}_value", node->name));
    function.entry.CreateStore(input, value);

    auto scopeLifetime = std::make_shared<MultipleLifetime>();
    scopeLifetime->push_back(makeAnonymousLifetime(node->type, node));
    scope.lifetimes[node] = std::move(scopeLifetime);

    lifetime = makeExpressionLifetime();
}
