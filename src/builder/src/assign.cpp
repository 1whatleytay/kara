#include <builder/builder.h>

#include <builder/error.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/reference.h>

// this copies :flushed:
std::optional<BuilderResult> BuilderScope::convert(
    const BuilderResult &result, const Typename &type, const Node *node) {
    if (result.type == type)
        return result;

    auto *typeRef = std::get_if<ReferenceTypename>(&type);
    auto *resultRef = std::get_if<ReferenceTypename>(&result.type);

    // Demote reference
    if (resultRef && *resultRef->value == type) {
        return BuilderResult(
            BuilderResult::Kind::Reference,
            get(result),
            type
        );
    }

    // Promote reference
    if (typeRef && *typeRef->value == result.type) {
        return BuilderResult(
            BuilderResult::Kind::Raw,
            ref(result, node),
            type
        );
    }

    if (result.type == types::null() && typeRef) {
        return BuilderResult(
            BuilderResult::Kind::Raw,
            current.CreatePointerCast(get(result), function.builder.makeTypename(type, node)),
            type
        );
    }

    if (type == types::boolean() && resultRef) {
        return BuilderResult(
            BuilderResult::Kind::Raw,
            current.CreateIsNotNull(get(result)),
            type
        );
    }

    // look through conversion operators...
    // convert reference to raw
    // convert raw to reference

    // all has gone to hell, error
    return std::nullopt;
}

void BuilderScope::makeAssign(const AssignNode *node) {
    BuilderResult destination = makeExpression(node->children.front()->as<ExpressionNode>());

    BuilderResult sourceRaw = makeExpression(node->children.back()->as<ExpressionNode>());
    std::optional<BuilderResult> sourceConverted = convert(sourceRaw, destination.type, node);

    if (!sourceConverted.has_value()) {
        throw VerifyError(node, "Assignment of type {} to {} is not allowed.",
            toString(sourceRaw.type), toString(destination.type));
    }

    BuilderResult source = std::move(*sourceConverted);

    if (destination.kind != BuilderResult::Kind::Reference) {
        throw VerifyError(node, "Left side of assign expression must be some variable or reference.");
    }

    current.CreateStore(get(source), destination.value);
}
