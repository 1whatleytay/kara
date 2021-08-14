#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/literals.h>
#include <parser/function.h>

std::optional<BuilderResult> BuilderScope::convert(const BuilderResult &r, const Typename &type, bool force) {
    BuilderResult result = r;

    if (result.type == type)
        return result;

    auto typeRef = std::get_if<ReferenceTypename>(&type);
    auto resultRef = std::get_if<ReferenceTypename>(&result.type);

    auto typePrim = std::get_if<PrimitiveTypename>(&type);
    auto resultPrim = std::get_if<PrimitiveTypename>(&result.type);

    if (force) {
        if (typeRef && resultRef) {
            return BuilderResult(
                BuilderResult::FlagTemporary,
                current ? current->CreateBitCast(get(result), builder.makeTypename(type)) : nullptr,
                type,
                &statementContext
            );
        }

        if (typePrim && resultRef && typePrim->type == PrimitiveType::ULong) {
            return BuilderResult(
                BuilderResult::FlagTemporary,
                current ? current->CreatePtrToInt(get(result), builder.makeTypename(type)) : nullptr,
                type,
                &statementContext
            );
        }

        if (typeRef && resultPrim && resultPrim->type == PrimitiveType::ULong) {
            return BuilderResult(
                BuilderResult::FlagTemporary,
                current ? current->CreateIntToPtr(get(result), builder.makeTypename(type)) : nullptr,
                type,
                &statementContext
            );
        }
    }

    // Demote reference
    if (resultRef && *resultRef->value == type) {
        return BuilderResult(
            BuilderResult::FlagReference | (resultRef->isMutable ? BuilderResult::FlagMutable : 0),
            get(result),
            type,
            &statementContext
        );
    }

    // Promote reference
    if (typeRef
        && *typeRef->value == result.type
        && typeRef->kind == ReferenceKind::Regular
        && (!typeRef->isMutable || result.isSet(BuilderResult::FlagMutable))) {
        // dont promote to unique or shared
        return BuilderResult(
            BuilderResult::FlagTemporary,
            ref(result),
            type,
            &statementContext
        );
    }

    if (typeRef && !resultRef && result.isSet(BuilderResult::FlagReference)) {
        result = BuilderResult(
            BuilderResult::FlagTemporary,
            result.value,
            ReferenceTypename {
                std::make_shared<Typename>(result.type),
                result.isSet(BuilderResult::FlagMutable)
            },
            &statementContext
        );

        resultRef = std::get_if<ReferenceTypename>(&result.type);
        resultPrim = nullptr;
    }

    if (typeRef && resultRef) {
        if (*typeRef->value == PrimitiveTypename::from(PrimitiveType::Any)) {
            return BuilderResult(
                BuilderResult::FlagTemporary,
                current ? current->CreatePointerCast(get(result), builder.makeTypename(type)) : nullptr,
                type,
                &statementContext
            );
        }

        auto typeArray = std::get_if<ArrayTypename>(typeRef->value.get());

        if (typeArray && typeArray->kind == ArrayKind::Unbounded) {
            if (*typeArray->value == *resultRef->value) {
                return BuilderResult(
                    BuilderResult::FlagTemporary,
                    get(result),
                    type,
                    &statementContext
                );
            }

            auto resultArray = std::get_if<ArrayTypename>(resultRef->value.get());

            if (resultArray && *typeArray->value == *resultArray->value
                && resultArray->kind == ArrayKind::FixedSize) {
                return BuilderResult(
                    BuilderResult::FlagTemporary,
                    current ? current->CreateStructGEP(get(result), 0) : nullptr,
                    type,
                    &statementContext
                );
            }
        }
    }

    if (result.type == PrimitiveTypename::from(PrimitiveType::Null) && typeRef) {
        return BuilderResult(
            BuilderResult::FlagTemporary,
            current ? current->CreatePointerCast(get(result), builder.makeTypename(type)) : nullptr,
            type,
            &statementContext
        );
    }

    if (type == PrimitiveTypename::from(PrimitiveType::Bool) && resultRef) {
        return BuilderResult(
            BuilderResult::FlagTemporary,
            current ? current->CreateIsNotNull(get(result)) : nullptr,
            type,
            &statementContext
        );
    }

    if (typePrim && resultPrim) {
        if (typePrim->isNumber() && resultPrim->isNumber()) {
            Type *dest = builder.makePrimitiveType(typePrim->type);

            if (resultPrim->isInteger() && typePrim->isFloat()) { // int -> float
                return BuilderResult(
                    BuilderResult::FlagTemporary,
                    current
                        ? resultPrim->isSigned()
                        ? current->CreateSIToFP(get(result), dest)
                        : current->CreateUIToFP(get(result), dest)
                        : nullptr,
                    type,
                    &statementContext
                );
            }

            if (resultPrim->isFloat() && typePrim->isInteger()) { // float -> int
                return BuilderResult(
                    BuilderResult::FlagTemporary,
                    current
                        ? typePrim->isSigned()
                        ? current->CreateFPToSI(get(result), dest)
                        : current->CreateFPToUI(get(result), dest)
                        : nullptr,
                    type,
                    &statementContext
                );
            }

            // promote or demote
            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? typePrim->isFloat()
                    ? resultPrim->priority() > typePrim->priority()
                        ? current->CreateFPTrunc(get(result), dest)
                        : current->CreateFPExt(get(result), dest)
                    : typePrim->isSigned()
                        ? current->CreateSExtOrTrunc(get(result), dest)
                        : current->CreateZExtOrTrunc(get(result), dest)
                    : nullptr,
                type,
                &statementContext
            );
        }
    }

    // look through conversion operators...
    // convert reference to raw
    // convert raw to reference

    // all has gone to hell, error
    return std::nullopt;
}

std::optional<Typename> BuilderScope::negotiate(const Typename &left, const Typename &right) {
    if (left == right)
        return left;

    auto leftPrim = std::get_if<PrimitiveTypename>(&left);
    auto rightPrim = std::get_if<PrimitiveTypename>(&right);

    if (leftPrim && rightPrim && leftPrim->isNumber() && rightPrim->isNumber()) {
        int32_t leftSize = leftPrim->size();
        int32_t rightSize = rightPrim->size();

        bool leftFloat = leftPrim->isFloat();
        bool rightFloat = rightPrim->isFloat();

        bool leftSign = leftPrim->isSigned();
        bool rightSign = rightPrim->isSigned();

        int32_t size = std::max(leftSize, rightSize);
        bool isSigned = leftSign || rightSign;
        bool isFloat = leftFloat || rightFloat;

        PrimitiveType type = ([&]() {
            if (isFloat) {
                switch (size) {
                    case 8: return PrimitiveType::Double;
                    case 4: return PrimitiveType::Float;
                    default: throw;
                }
            }

            if (isSigned) {
                switch (size) {
                    case 8: return PrimitiveType::Long;
                    case 4: return PrimitiveType::Int;
                    case 2: return PrimitiveType::Short;
                    case 1: return PrimitiveType::Byte;
                    default: throw;
                }
            } else {
                switch (size) {
                    case 8: return PrimitiveType::ULong;
                    case 4: return PrimitiveType::UInt;
                    case 2: return PrimitiveType::UShort;
                    case 1: return PrimitiveType::UByte;
                    default: throw;
                }
            }
        })();

        return PrimitiveTypename { type };
    }

    return std::nullopt;
}

std::optional<std::pair<BuilderResult, BuilderResult>> BuilderScope::convert(
    const BuilderResult &a, BuilderScope &aScope,
    const BuilderResult &b, BuilderScope &bScope) {
    std::optional<Typename> mediator = negotiate(a.type, b.type);

    if (!mediator)
        return std::nullopt;

    auto left = aScope.convert(a, *mediator);
    auto right = bScope.convert(b, *mediator);

    if (!left || !right)
        return std::nullopt;

    return std::make_pair(*left, *right);
}

std::optional<std::pair<BuilderResult, BuilderResult>> BuilderScope::convert(
    const BuilderResult &a, const BuilderResult &b) {
    return convert(a, *this, b, *this);
}

void BuilderScope::invokeDestroy(const BuilderResult &result) {
    invokeDestroy(result, exitChainBegin);
}

void BuilderScope::invokeDestroy(const BuilderResult &result, BasicBlock *block) {
    IRBuilder<> irBuilder(builder.context);
    irBuilder.SetInsertPoint(block, block->begin());

    invokeDestroy(result, irBuilder);
}

void BuilderScope::invokeDestroy(const BuilderResult &result, IRBuilder<> &irBuilder) {
    if (!current)
        return;

    auto referenceTypename = std::get_if<ReferenceTypename>(&result.type);

    if (referenceTypename) {
        if (referenceTypename->kind == ReferenceKind::Unique) {
            auto free = builder.getFree();
            auto pointer = irBuilder.CreatePointerCast(get(result), Type::getInt8PtrTy(builder.context));

            // alternative is keep Kind::Reference but only CreateLoad(result.value)
            auto containedValue = BuilderResult(
                BuilderResult::FlagTemporary,
                irBuilder.CreateLoad(get(result), "invokeDestroy_load"),
                *referenceTypename->value,
                nullptr // dont do it!! &statementContext would be here but i want raw
            );

            invokeDestroy(containedValue, irBuilder);

            irBuilder.CreateCall(free, { pointer });
        }
    } else if (!builder.destroyInvokables.empty()) {
        try {
            call(builder.destroyInvokables, { { &result }, { } }, &irBuilder);

            if (auto named = std::get_if<NamedTypename>(&result.type)) {
                auto containedType = named->type;
                auto builderType = builder.makeType(containedType);

                auto func = builderType->implicitDestructor->function;

                Value *param = ref(result, irBuilder);

                // duplicate sanity check
                {
                    auto paramType = param->getType();
                    assert(paramType->isPointerTy());

                    auto pointeeType = paramType->getPointerElementType();
                    assert(pointeeType == builderType->type);
                }

                irBuilder.CreateCall(func, { param });
            }
        } catch (const std::runtime_error &e) { }
    }

    // TODO: needs call to implicit object destructor, probably in BuilderType, can do later
    // ^^ outside of try catch, so call doesn't fail and cancel destructing an object
    // we can also make builder function null if we dont have anything to destroy, type X { a int, b int, c int } but idk if that's worth
}

BuilderResult BuilderScope::unpack(const BuilderResult &result) {
    BuilderResult value = result;

    while (auto *r = std::get_if<ReferenceTypename>(&value.type)) {
        value.implicit = nullptr;

        value.flags |= BuilderResult::FlagReference;

        if (value.isSet(BuilderResult::FlagReference))
            value.value = current ? current->CreateLoad(value.value) : nullptr;

        value.type = *r->value;
    }

    return value;
}

// remove from statement scope or call move operator
BuilderResult BuilderScope::pass(const BuilderResult &result) {
    assert(!result.isSet(BuilderResult::FlagUnresolved));

    auto reference = std::get_if<ReferenceTypename>(&result.type);

    if (reference && reference->kind != ReferenceKind::Regular) {
        statementContext.avoidDestroy.insert(result.statementUID);
    }

    return result;
}

BuilderResult BuilderScope::infer(const BuilderResult &result) {
    if (result.isSet(BuilderResult::FlagUnresolved)) {
        // First variable in scope search will be lowest in scope.
        auto varIterator = std::find_if(result.references.begin(), result.references.end(), [](const Node *node) {
            return node->is(Kind::Variable);
        });

        if (varIterator != result.references.end()) {
            auto var = (*varIterator)->as<VariableNode>();

            BuilderVariable *info;

            if (var->parent->is(Kind::Root))
                info = builder.makeGlobal(var); // AH THIS WONT WORK FOR EXTERNAL
            else
                info = findVariable(var);

            if (!info)
                throw VerifyError(var, "Cannot find variable reference.");

            return BuilderResult(
                BuilderResult::FlagReference | (info->node->isMutable ? BuilderResult::FlagMutable : 0),

                info->value,
                info->type,
                &statementContext
            );
        }

        auto newIterator = std::find_if(result.references.begin(), result.references.end(), [](const Node *node) {
            return node->is(Kind::New);
        });

        if (newIterator != result.references.end())
            return makeNew((*newIterator)->as<NewNode>());

        std::vector<const Node *> functions;

        auto callable = [](const Node *n) { return n->is(Kind::Function) || n->is(Kind::Type); };

        std::copy_if(result.references.begin(), result.references.end(),
            std::back_inserter(functions), callable);

        if (!functions.empty()) {
            std::vector<const BuilderResult *> params;

            if (result.implicit)
                params.push_back(result.implicit.get());

            return callUnpack(call(functions, { params, { } }), result.from);
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
            BuilderResult::FlagTemporary,
            current ? current->CreateCall(reinterpret_cast<Function *>(result.value), params) : nullptr,
            *functionTypename->returnType,
            &statementContext
        );
    }

    return result;
}

BuilderResult BuilderScope::makeNew(const NewNode *node) {
    auto type = builder.resolveTypename(node->type());
    auto llvmType = builder.makeTypename(type);
    auto pointerType = PointerType::get(llvmType, 0);

    size_t bytes = builder.file.manager.target.layout->getTypeStoreSize(llvmType);
    auto malloc = builder.getMalloc();

    auto constant = ConstantInt::get(Type::getInt64Ty(builder.context), bytes);

    return BuilderResult(
        BuilderResult::FlagTemporary,

        current ? current->CreatePointerCast(current->CreateCall(malloc, { constant }), pointerType) : nullptr,
        ReferenceTypename { std::make_shared<Typename>(type), true, ReferenceKind::Unique },
        &statementContext
    );
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

    if (!destination.isSet(BuilderResult::FlagReference) || !destination.isSet(BuilderResult::FlagMutable)) {
        throw VerifyError(node, "Left side of assign expression must be a mutable variable.");
    }

    if (current) {
        Value *result;

        try {
            if (node->op == AssignNode::Operator::Assign) {
                result = get(pass(source));
            } else {
                auto operation = ([op = node->op]() {
                    switch (op) {
                        case AssignNode::Operator::Plus: return OperatorNode::Operation::Add;
                        case AssignNode::Operator::Minus: return OperatorNode::Operation::Sub;
                        case AssignNode::Operator::Multiply: return OperatorNode::Operation::Mul;
                        case AssignNode::Operator::Divide: return OperatorNode::Operation::Div;
                        case AssignNode::Operator::Modulo: return OperatorNode::Operation::Mod;
                        default: throw std::runtime_error("Unimplemented assign node operator.");
                    }
                })();

                result = get(pass(combine(destination, source, operation)));
            }
        } catch (const std::runtime_error &e) {
            throw VerifyError(node, "{}", e.what());
        }

        current->CreateStore(result, destination.value);
    }
}
