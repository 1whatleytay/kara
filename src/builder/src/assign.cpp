#include <builder/builder.h>

#include <builder/error.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/reference.h>

// this copies :flushed:
std::optional<BuilderResult> BuilderScope::convert(const BuilderResult &result, const Typename &type) {
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
            ref(result),
            type
        );
    }

    if (result.type == types::null() && typeRef) {
        return BuilderResult(
            BuilderResult::Kind::Raw,
            current.CreatePointerCast(get(result), function.builder.makeTypename(type)),
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

    if (types::isNumber(result.type) && types::isNumber(type)) {
        Type *dest = function.builder.makeBuiltinTypename(std::get<StackTypename>(type));

        if (types::isInteger(result.type) && types::isFloat(type)) { // int -> float
            return BuilderResult(
                BuilderResult::Kind::Raw,
                types::isSigned(result.type)
                    ? current.CreateSIToFP(get(result), dest)
                    : current.CreateUIToFP(get(result), dest),
                type
            );
        }

        if (types::isFloat(result.type) && types::isInteger(type)) { // float -> int
            return BuilderResult(
                BuilderResult::Kind::Raw,
                types::isSigned(type)
                    ? current.CreateFPToSI(get(result), dest)
                    : current.CreateFPToUI(get(result), dest),
                type
            );
        }

        // promote or demote
        return BuilderResult(
            BuilderResult::Kind::Raw,
            types::isFloat(type)
                ? types::priority(result.type) > types::priority(type)
                ? current.CreateFPTrunc(get(result), dest)
                : current.CreateFPExt(get(result), dest)
                : types::isSigned(type)
                ? current.CreateSExtOrTrunc(get(result), dest)
                : current.CreateZExtOrTrunc(get(result), dest),
            type
        );
    }

    // look through conversion operators...
    // convert reference to raw
    // convert raw to reference

    // all has gone to hell, error
    return std::nullopt;
}

std::optional<std::pair<BuilderResult, BuilderResult>> BuilderScope::convert(
    const BuilderResult &a, BuilderScope &aScope,
    const BuilderResult &b, BuilderScope &bScope) {
    std::optional<BuilderResult> medium;

    // Try to convert second first, so it's at least consistent.
    medium = bScope.convert(b, a.type);
    if (medium.has_value())
        return std::make_pair(a, medium.value());

    medium = aScope.convert(a, b.type);
    if (medium.has_value())
        return std::make_pair(medium.value(), b);

    return std::nullopt;
}

std::optional<std::pair<BuilderResult, BuilderResult>> BuilderScope::convert(
    const BuilderResult &a, const BuilderResult &b) {
    return convert(a, *this, b, *this);
}

void BuilderScope::makeAssign(const AssignNode *node) {
    BuilderResult destination = makeExpression(node->children.front()->as<ExpressionNode>());

    BuilderResult sourceRaw = makeExpression(node->children.back()->as<ExpressionNode>());
    std::optional<BuilderResult> sourceConverted = convert(sourceRaw, destination.type);

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
