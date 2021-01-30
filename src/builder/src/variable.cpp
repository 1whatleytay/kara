#include <builder/builder.h>

#include <builder/error.h>

#include <parser/variable.h>

#include <fmt/format.h>

std::shared_ptr<MultipleLifetime> BuilderVariable::makeExpressionLifetime() const {
    auto base = std::make_shared<MultipleLifetime>();
    auto child = std::make_shared<MultipleLifetime>();

    base->push_back(std::make_shared<ReferenceLifetime>(child, PlaceholderId { nullptr, 0 }));
    child->push_back(std::make_shared<VariableLifetime>(node));

    return base;
}

BuilderVariable::BuilderVariable(const VariableNode *node, BuilderScope &scope)
    : function(scope.function), node(node) {
    assert(node->fixedType || !node->children.empty());

    std::optional<BuilderResult> possibleDefault;

    if (!node->children.empty()) {
        BuilderResult result = scope.makeExpression(node->children.front()->as<ExpressionNode>()->result);
        possibleDefault = result; // copy :|

        if (node->fixedType) {
            std::optional<BuilderResult> resultConverted = scope.convert(result, *node->fixedType);

            if (!resultConverted) {
                throw VerifyError(node->children.front().get(),
                    "Cannot convert from type {} to variable fixed type {}.",
                    toString(result.type),
                    toString(node->fixedType.value()));
            }

            result = *resultConverted;
        }

        type = result.type;
    } else {
        type = node->fixedType.value();
    }

    value = function.entry.CreateAlloca(function.builder.makeTypename(type), nullptr, node->name);

    auto scopeLifetime = std::make_shared<MultipleLifetime>();
    std::shared_ptr<Lifetime> defaultLifetime = makeDefaultLifetime(type, { node, 0 });
    if (defaultLifetime)
        scopeLifetime->push_back(std::move(defaultLifetime));

    scope.lifetimes[node] = std::move(scopeLifetime);

    lifetime = makeExpressionLifetime();

    if (possibleDefault) {
        const BuilderResult &result = *possibleDefault;

        // could be abstracted out to some assign method, used in makeAssign and makeStatement
        std::vector<MultipleLifetime *> sourceLifetimes =
            scope.expand({ result.lifetime.get() }, result.lifetimeDepth + 1, true);
        std::vector<MultipleLifetime *> destinationLifetimes =
            scope.expand({ lifetime.get() }, 2, true);

        for (auto dest : destinationLifetimes) {
            dest->clear();

            for (auto src : sourceLifetimes) {
                dest->insert(dest->begin(), src->begin(), src->end());
            }

            dest->simplify();
        }

        scope.current.CreateStore(scope.get(result), value);
    }
}

BuilderVariable::BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope)
    : function(scope.function), node(node) {
    if (!node->fixedType || !node->children.empty())
        throw VerifyError(node, "A function parameter must have fixed type and no default value, unimplemented.");

    type = *node->fixedType;

    value = function.entry.CreateAlloca(
        function.builder.makeTypename(type), nullptr, fmt::format("{}_value", node->name));
    function.entry.CreateStore(input, value);

    auto scopeLifetime = std::make_shared<MultipleLifetime>();

    if (auto x = makeAnonymousLifetime(*node->fixedType, { node, 0 }))
        scopeLifetime->push_back(std::move(x));

    scope.lifetimes[node] = std::move(scopeLifetime);

    lifetime = makeExpressionLifetime();
}
