#include <builder/builder.h>

#include <builder/error.h>

#include <parser/variable.h>

#include <fmt/format.h>

BuilderVariable::BuilderVariable(const VariableNode *node, BuilderScope &scope)
    : function(scope.function), node(node) {
    assert(node->fixedType || !node->children.empty());

    std::optional<BuilderResult> possibleDefault;

    if (!node->children.empty()) {
        BuilderResult result = scope.makeExpression(node->children.front()->as<ExpressionNode>()->result);
        possibleDefault = result; // copy :|

        if (node->fixedType) {
            std::optional<BuilderResult> resultConverted = scope.convert(result, *node->fixedType, node);

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

    value = function.entry.CreateAlloca(function.builder.makeTypename(type, node), nullptr, node->name);

    if (possibleDefault)
        scope.current.CreateStore(scope.get(*possibleDefault), value);
}

BuilderVariable::BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope)
    : function(scope.function), node(node) {
    if (!node->fixedType || !node->children.empty())
        throw VerifyError(node, "A function parameter must have fixed type and no default value, unimplemented.");

    type = *node->fixedType;

    value = function.entry.CreateAlloca(
        function.builder.makeTypename(type, node), nullptr, fmt::format("{}_value", node->name));
    function.entry.CreateStore(input, value);
}
