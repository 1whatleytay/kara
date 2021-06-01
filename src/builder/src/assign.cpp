#include <builder/builder.h>

#include <builder/error.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/literals.h>
#include <parser/function.h>

// this copies :flushed:
std::optional<BuilderResult> BuilderScope::convert(const BuilderResult &r, const Typename &type, bool force) {
    BuilderResult result = r;

    if (result.type == type)
        return result;

    auto typeRef = std::get_if<ReferenceTypename>(&type);
    auto resultRef = std::get_if<ReferenceTypename>(&result.type);

    auto otherPrim = std::get_if<PrimitiveTypename>(&type);
    auto resultPrim = std::get_if<PrimitiveTypename>(&result.type);

    if (force) {
        if (typeRef && resultRef) {
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current ? current->CreateBitCast(get(result), function.builder.makeTypename(type)) : nullptr,
                type
            );
        }

        if (typeRef && resultPrim && resultPrim->type == PrimitiveType::ULong) {
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current ? current->CreateIntToPtr(get(result), function.builder.makeTypename(type)) : nullptr,
                type
            );
        }
    }

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

    if (typeRef && !resultRef && result.kind != BuilderResult::Kind::Raw) {
        result = BuilderResult(
            BuilderResult::Kind::Raw,
            result.value,
            ReferenceTypename {
                std::make_shared<Typename>(result.type),
                result.kind == BuilderResult::Kind::Reference
            }
        );

        resultRef = std::get_if<ReferenceTypename>(&result.type);
        resultPrim = nullptr;
    }

    if (typeRef && resultRef) {
        if (*typeRef->value == PrimitiveTypename::from(PrimitiveType::Any)) {
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current ? current->CreatePointerCast(get(result), function.builder.makeTypename(type)) : nullptr,
                type
            );
        }

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

            default:
                assert(false);
        }

        value.type = *r->value;
    }

    return value;
}

BuilderResult BuilderScope::infer(const BuilderResult &result) {
    if (result.kind == BuilderResult::Kind::Unresolved) {
        // First variable in scope search will be lowest in scope.
        auto iterator = std::find_if(result.references.begin(), result.references.end(), [](const Node *node) {
            return node->is(Kind::Variable);
        });

        if (iterator != result.references.end()) {
            auto var = (*iterator)->as<VariableNode>();

            BuilderVariable *info;

            if (var->parent->is(Kind::Root))
                info = function.builder.makeGlobal(var); // AHHH THIS WONT WORK FOR EXTERNALss
            else
                info = findVariable(var);

            if (!info)
                throw VerifyError(var, "Cannot find variable reference.");

            return BuilderResult(
                BuilderResult::Kind::Reference,

                info->value,
                info->type
            );
        }

        std::vector<const FunctionNode *> functions;

        for (auto node : result.references) {
            if (!node->is(Kind::Function))
                continue;

            functions.push_back(node->as<FunctionNode>());
        }

        if (!functions.empty()) {
            std::vector<BuilderResult *> params;

            if (result.implicit)
                params.push_back(result.implicit.get());

            try {
                return call(functions, params);
            } catch (const std::runtime_error &e) {
                throw VerifyError(result.from, "{}", e.what());
            }
        } else {
            throw VerifyError(result.from, "Reference does not implicitly resolve to anything.");
        }

        // if variable, return first
        // if function, find one with 0 params or infer params, e.g. some version of match is needed
    }

    const FunctionTypename *functionTypename = std::get_if<FunctionTypename>(&result.type);

    if (functionTypename && functionTypename->kind == FunctionTypename::Kind::Pointer) {
        std::vector<Value *> params;

        if (result.implicit)
            params.push_back(get(*result.implicit));

        return BuilderResult(
            BuilderResult::Kind::Raw,
            current ? current->CreateCall(reinterpret_cast<Function *>(result.value), params) : nullptr,
            *functionTypename->returnType
        );
    }

    return result;
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
