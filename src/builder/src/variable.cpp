#include <builder/builder.h>

#include <builder/error.h>

#include <parser/variable.h>

#include <fmt/format.h>

BuilderVariable::BuilderVariable(const VariableNode *node, BuilderScope &scope)
    : function(scope.function), node(node) {
    assert(node->hasFixedType || node->value());

    std::optional<BuilderResult> possibleDefault;

    if (node->value()) {
        BuilderResult result = scope.makeExpression(node->value());
        possibleDefault = result; // copy :|

        if (node->hasFixedType) {
            auto fixedType = function.builder.resolveTypename(node->fixedType());

            std::optional<BuilderResult> resultConverted = scope.convert(result, fixedType);

            if (!resultConverted) {
                throw VerifyError(node->value(),
                    "Cannot convert from type {} to variable fixed type {}.",
                    toString(result.type),
                    toString(fixedType));
            }

            result = *resultConverted;
        }

        type = result.type;
    } else {
        type = function.builder.resolveTypename(node->fixedType());
    }

    if (scope.current) {
        value = function.entry.CreateAlloca(function.builder.makeTypename(type), nullptr, node->name);

        if (possibleDefault)
            scope.current->CreateStore(scope.get(*possibleDefault), value);
    }
}

BuilderVariable::BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope)
    : function(scope.function), node(node) {
    if (!node->hasFixedType || node->value())
        throw VerifyError(node, "A function parameter must have fixed type and no default value, unimplemented.");

    type = function.builder.resolveTypename(node->fixedType());

    if (scope.current) {
        value = function.entry.CreateAlloca(
            function.builder.makeTypename(type), nullptr, fmt::format("{}_value", node->name));
        function.entry.CreateStore(input, value);
    }
}
