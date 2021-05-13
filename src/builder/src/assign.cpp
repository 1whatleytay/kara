#include <builder/builder.h>

#include <builder/error.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/literals.h>

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

    if (typeRef && resultRef) {
        auto typeArray = std::get_if<ArrayTypename>(typeRef->value.get());

        if (typeArray && typeArray->kind == ArrayKind::Unbounded) {
            if (*typeArray->value == *resultRef->value) {
                return BuilderResult(
                    BuilderResult::Kind::Raw,
                    get(result),
                    type
                );
            }

            auto resultArray = std::get_if<ArrayTypename>(resultRef->value.get());

            if (resultArray && *typeArray->value == *resultArray->value
                && resultArray->kind == ArrayKind::FixedSize) {
                return BuilderResult(
                    BuilderResult::Kind::Raw,
                    current ? current->CreateStructGEP(get(result), 0) : nullptr,
                    type
                );
            }
        }
    }

    if (result.type == PrimitiveTypename::from(PrimitiveType::Null) && typeRef) {
        return BuilderResult(
            BuilderResult::Kind::Raw,
            current ? current->CreatePointerCast(get(result), function.builder.makeTypename(type)) : nullptr,
            type
        );
    }

    if (type == PrimitiveTypename::from(PrimitiveType::Bool) && resultRef) {
        return BuilderResult(
            BuilderResult::Kind::Raw,
            current ? current->CreateIsNotNull(get(result)) : nullptr,
            type
        );
    }

    auto otherPrim = std::get_if<PrimitiveTypename>(&type);
    auto resultPrim = std::get_if<PrimitiveTypename>(&result.type);

    if (otherPrim && resultPrim) {
        if (otherPrim->isNumber() && resultPrim->isNumber()) {
            Type *dest = function.builder.makePrimitiveType(otherPrim->type);

            if (resultPrim->isInteger() && otherPrim->isFloat()) { // int -> float
                return BuilderResult(
                    BuilderResult::Kind::Raw,
                    current
                        ? resultPrim->isSigned()
                        ? current->CreateSIToFP(get(result), dest)
                        : current->CreateUIToFP(get(result), dest)
                        : nullptr,
                    type
                );
            }

            if (resultPrim->isFloat() && otherPrim->isInteger()) { // float -> int
                return BuilderResult(
                    BuilderResult::Kind::Raw,
                    current
                        ? otherPrim->isSigned()
                        ? current->CreateFPToSI(get(result), dest)
                        : current->CreateFPToUI(get(result), dest)
                        : nullptr,
                    type
                );
            }

            // promote or demote
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? otherPrim->isFloat()
                    ? resultPrim->priority() > otherPrim->priority()
                        ? current->CreateFPTrunc(get(result), dest)
                        : current->CreateFPExt(get(result), dest)
                    : otherPrim->isSigned()
                        ? current->CreateSExtOrTrunc(get(result), dest)
                        : current->CreateZExtOrTrunc(get(result), dest)
                    : nullptr,
                type
            );
        }
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

BuilderResult BuilderScope::convertOrThrow(const Node *node, const BuilderResult &result, const Typename &type) {
    std::optional<BuilderResult> value = convert(result, type);

    if (!value)
        throw VerifyError(node, "Expected type {}, type is {}.", toString(type), toString(result.type));

    return *value;
}

BuilderResult BuilderScope::unpack(const BuilderResult &result) {
    BuilderResult value = result;

    while (auto *r = std::get_if<ReferenceTypename>(&value.type)) {
        value.implicit = nullptr;

        switch (value.kind) {
            case BuilderResult::Kind::Raw:
                value.kind = BuilderResult::Kind::Reference;
                break;
            case BuilderResult::Kind::Literal:
            case BuilderResult::Kind::Reference:
                value.kind = BuilderResult::Kind::Reference;
                value.value = current ? current->CreateLoad(value.value) : nullptr;
                break;
        }

        value.type = *r->value;
    }

    return value;
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


    if (current) {
        Value *result;

        try {
            switch (node->op) {
                case AssignNode::Operator::Assign:
                    result = get(source);
                    break;

                case AssignNode::Operator::Plus:
                    result = get(combine(source, destination, OperatorNode::Operation::Add));
                    break;

                case AssignNode::Operator::Minus:
                    result = get(combine(source, destination, OperatorNode::Operation::Sub));
                    break;

                case AssignNode::Operator::Multiply:
                    result = get(combine(source, destination, OperatorNode::Operation::Mul));
                    break;

                case AssignNode::Operator::Divide:
                    result = get(combine(source, destination, OperatorNode::Operation::Div));
                    break;

                default:
                    throw std::exception();
            }
        } catch (const std::runtime_error &e) {
            throw VerifyError(node, "{}", e.what());
        }

        current->CreateStore(result, destination.value);
    }
}
