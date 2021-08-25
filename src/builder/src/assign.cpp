#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/assign.h>
#include <parser/expression.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/variable.h>

namespace kara::builder {
    std::optional<builder::Result> Scope::convert(const builder::Result &r, const utils::Typename &type, bool force) {
        builder::Result result = r;

        if (result.type == type)
            return result;

        auto typeRef = std::get_if<utils::ReferenceTypename>(&type);
        auto resultRef = std::get_if<utils::ReferenceTypename>(&result.type);

        auto typePrim = std::get_if<utils::PrimitiveTypename>(&type);
        auto resultPrim = std::get_if<utils::PrimitiveTypename>(&result.type);

        if (force) {
            if (typeRef && resultRef) {
                return builder::Result(builder::Result::FlagTemporary,
                    current ? current->CreateBitCast(get(result), builder.makeTypename(type)) : nullptr, type,
                    &statementContext);
            }

            if (typePrim && resultRef && typePrim->type == utils::PrimitiveType::ULong) {
                return builder::Result(builder::Result::FlagTemporary,
                    current ? current->CreatePtrToInt(get(result), builder.makeTypename(type)) : nullptr, type,
                    &statementContext);
            }

            if (typeRef && resultPrim && resultPrim->type == utils::PrimitiveType::ULong) {
                return builder::Result(builder::Result::FlagTemporary,
                    current ? current->CreateIntToPtr(get(result), builder.makeTypename(type)) : nullptr, type,
                    &statementContext);
            }
        }

        // Demote reference
        if (resultRef && *resultRef->value == type) {
            return builder::Result(
                builder::Result::FlagReference | (resultRef->isMutable ? builder::Result::FlagMutable : 0), get(result),
                type, &statementContext);
        }

        // Promote reference
        if (typeRef && *typeRef->value == result.type && typeRef->kind == utils::ReferenceKind::Regular
            && (!typeRef->isMutable || result.isSet(builder::Result::FlagMutable))) {
            // dont promote to unique or shared
            return builder::Result(builder::Result::FlagTemporary, ref(result), type, &statementContext);
        }

        if (typeRef && !resultRef && result.isSet(builder::Result::FlagReference)) {
            result = builder::Result(builder::Result::FlagTemporary, result.value,
                utils::ReferenceTypename {
                    std::make_shared<utils::Typename>(result.type), result.isSet(builder::Result::FlagMutable) },
                &statementContext);

            resultRef = std::get_if<utils::ReferenceTypename>(&result.type);
            resultPrim = nullptr;
        }

        if (typeRef && resultRef) {
            // YIKEs
            if (*typeRef->value == *resultRef->value && typeRef->kind == utils::ReferenceKind::Regular
                && resultRef->isMutable) {
                return builder::Result(builder::Result::FlagTemporary, get(result), type, &statementContext);
            }

            if (*typeRef->value == utils::PrimitiveTypename::from(utils::PrimitiveType::Any)) {
                return builder::Result(builder::Result::FlagTemporary,
                    current ? current->CreatePointerCast(get(result), builder.makeTypename(type)) : nullptr, type,
                    &statementContext);
            }

            auto typeArray = std::get_if<utils::ArrayTypename>(typeRef->value.get());

            if (typeArray && typeArray->kind == utils::ArrayKind::Unbounded) {
                if (*typeArray->value == *resultRef->value) {
                    return builder::Result(builder::Result::FlagTemporary, get(result), type, &statementContext);
                }

                auto resultArray = std::get_if<utils::ArrayTypename>(resultRef->value.get());

                if (resultArray && *typeArray->value == *resultArray->value
                    && resultArray->kind == utils::ArrayKind::FixedSize) {
                    return builder::Result(builder::Result::FlagTemporary,
                        current ? current->CreateStructGEP(get(result), 0) : nullptr, type, &statementContext);
                }
            }
        }

        if (result.type == utils::PrimitiveTypename::from(utils::PrimitiveType::Null) && typeRef) {
            return builder::Result(builder::Result::FlagTemporary,
                current ? current->CreatePointerCast(get(result), builder.makeTypename(type)) : nullptr, type,
                &statementContext);
        }

        if (type == utils::PrimitiveTypename::from(utils::PrimitiveType::Bool) && resultRef) {
            return builder::Result(builder::Result::FlagTemporary,
                current ? current->CreateIsNotNull(get(result)) : nullptr, type, &statementContext);
        }

        if (typePrim && resultPrim) {
            if (typePrim->isNumber() && resultPrim->isNumber()) {
                llvm::Type *dest = builder.makePrimitiveType(typePrim->type);

                if (resultPrim->isInteger() && typePrim->isFloat()) { // int -> float
                    return builder::Result(builder::Result::FlagTemporary,
                        current ? resultPrim->isSigned() ? current->CreateSIToFP(get(result), dest)
                                                         : current->CreateUIToFP(get(result), dest)
                                : nullptr,
                        type, &statementContext);
                }

                if (resultPrim->isFloat() && typePrim->isInteger()) { // float -> int
                    return builder::Result(builder::Result::FlagTemporary,
                        current ? typePrim->isSigned() ? current->CreateFPToSI(get(result), dest)
                                                       : current->CreateFPToUI(get(result), dest)
                                : nullptr,
                        type, &statementContext);
                }

                // promote or demote
                return builder::Result(builder::Result::FlagTemporary,
                    current ? typePrim->isFloat()  ? resultPrim->priority() > typePrim->priority()
                                 ? current->CreateFPTrunc(get(result), dest)
                                 : current->CreateFPExt(get(result), dest)
                             : typePrim->isSigned() ? current->CreateSExtOrTrunc(get(result), dest)
                                                   : current->CreateZExtOrTrunc(get(result), dest)
                            : nullptr,
                    type, &statementContext);
            }
        }

        // look through conversion operators...
        // convert reference to raw
        // convert raw to reference

        // all has gone to hell, error
        return std::nullopt;
    }

    std::optional<utils::Typename> Scope::negotiate(const utils::Typename &left, const utils::Typename &right) {
        if (left == right)
            return left;

        auto leftPrim = std::get_if<utils::PrimitiveTypename>(&left);
        auto rightPrim = std::get_if<utils::PrimitiveTypename>(&right);

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

            utils::PrimitiveType type = ([&]() {
                if (isFloat) {
                    switch (size) {
                    case 8:
                        return utils::PrimitiveType::Double;
                    case 4:
                        return utils::PrimitiveType::Float;
                    default:
                        throw;
                    }
                }

                if (isSigned) {
                    switch (size) {
                    case 8:
                        return utils::PrimitiveType::Long;
                    case 4:
                        return utils::PrimitiveType::Int;
                    case 2:
                        return utils::PrimitiveType::Short;
                    case 1:
                        return utils::PrimitiveType::Byte;
                    default:
                        throw;
                    }
                } else {
                    switch (size) {
                    case 8:
                        return utils::PrimitiveType::ULong;
                    case 4:
                        return utils::PrimitiveType::UInt;
                    case 2:
                        return utils::PrimitiveType::UShort;
                    case 1:
                        return utils::PrimitiveType::UByte;
                    default:
                        throw;
                    }
                }
            })();

            return utils::PrimitiveTypename { type };
        }

        return std::nullopt;
    }

    std::optional<std::pair<builder::Result, builder::Result>> Scope::convert(
        const builder::Result &a, Scope &aScope, const builder::Result &b, Scope &bScope) {
        std::optional<utils::Typename> mediator = negotiate(a.type, b.type);

        if (!mediator)
            return std::nullopt;

        auto left = aScope.convert(a, *mediator);
        auto right = bScope.convert(b, *mediator);

        if (!left || !right)
            return std::nullopt;

        return std::make_pair(*left, *right);
    }

    std::optional<std::pair<builder::Result, builder::Result>> Scope::convert(
        const builder::Result &a, const builder::Result &b) {
        return convert(a, *this, b, *this);
    }

    void Scope::invokeDestroy(const builder::Result &result) { invokeDestroy(result, exitChainBegin); }

    void Scope::invokeDestroy(const builder::Result &result, llvm::BasicBlock *block) {
        llvm::IRBuilder<> irBuilder(builder.context);
        irBuilder.SetInsertPoint(block, block->begin());

        invokeDestroy(result, irBuilder);
    }

    void Scope::invokeDestroy(const builder::Result &result, llvm::IRBuilder<> &irBuilder) {
        if (!current)
            return;

        auto referenceTypename = std::get_if<utils::ReferenceTypename>(&result.type);

        if (referenceTypename) {
            if (referenceTypename->kind == utils::ReferenceKind::Unique) {
                auto free = builder.getFree();
                auto pointer = irBuilder.CreatePointerCast(get(result), llvm::Type::getInt8PtrTy(builder.context));

                // alternative is keep Kind::Reference but only CreateLoad(result.value)
                auto containedValue = builder::Result(builder::Result::FlagTemporary,
                    irBuilder.CreateLoad(get(result), "invokeDestroy_load"), *referenceTypename->value,
                    nullptr // dont do it!! &statementContext would be here but i want raw
                );

                invokeDestroy(containedValue, irBuilder);

                irBuilder.CreateCall(free, { pointer });
            }
        } else if (!builder.destroyInvokables.empty()) {
            try {
                call(builder.destroyInvokables, { { &result }, {} }, &irBuilder);

                if (auto named = std::get_if<utils::NamedTypename>(&result.type)) {
                    auto containedType = named->type;
                    auto builderType = builder.makeType(containedType);

                    auto func = builderType->implicitDestructor->function;

                    llvm::Value *param = ref(result, irBuilder);

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

        // TODO: needs call to implicit object destructor, probably in BuilderType,
        // can do later
        // ^^ outside of try catch, so call doesn't fail and cancel destructing an
        // object we can also make builder function null if we dont have anything to
        // destroy, type X { a int, b int, c int } but idk if that's worth
    }

    builder::Result Scope::unpack(const builder::Result &result) {
        builder::Result value = result;

        while (auto *r = std::get_if<utils::ReferenceTypename>(&value.type)) {
            value.flags &= ~(builder::Result::FlagMutable);
            value.flags |= r->isMutable ? builder::Result::FlagMutable : 0;
            value.flags |= builder::Result::FlagReference;

            if (value.isSet(builder::Result::FlagReference))
                value.value = current ? current->CreateLoad(value.value) : nullptr;

            value.type = *r->value;
        }

        return value;
    }

    // remove from statement scope or call move operator
    builder::Result Scope::pass(const builder::Result &result) {
        auto reference = std::get_if<utils::ReferenceTypename>(&result.type);

        if (reference && reference->kind != utils::ReferenceKind::Regular) {
            statementContext.avoidDestroy.insert(result.statementUID);
        }

        return result;
    }

    template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

    builder::Result Scope::infer(const Wrapped &result) {
        return std::visit(
            overloaded { [&](const builder::Unresolved &result) -> builder::Result {
                            // First variable in scope search will be lowest in scope.
                            auto isVariable = [](const hermes::Node *node) { return node->is(parser::Kind::Variable); };
                            auto varIterator
                                = std::find_if(result.references.begin(), result.references.end(), isVariable);

                            if (varIterator != result.references.end()) {
                                auto var = (*varIterator)->as<parser::Variable>();

                                builder::Variable *info;

                                if (var->parent->is(parser::Kind::Root))
                                    info = builder.makeGlobal(var); // AH THIS WONT WORK FOR EXTERNAL
                                else
                                    info = findVariable(var);

                                if (!info)
                                    throw VerifyError(var, "Cannot find variable reference.");

                                return builder::Result(builder::Result::FlagReference
                                        | (info->node->isMutable ? builder::Result::FlagMutable : 0),

                                    info->value, info->type, &statementContext);
                            }

                            auto isNew = [](const hermes::Node *node) { return node->is(parser::Kind::New); };
                            auto newIterator = std::find_if(result.references.begin(), result.references.end(), isNew);

                            if (newIterator != result.references.end())
                                return makeNew((*newIterator)->as<parser::New>());

                            std::vector<const hermes::Node *> functions;

                            auto callable = [](const hermes::Node *n) {
                                return n->is(parser::Kind::Function) || n->is(parser::Kind::Type);
                            };

                            std::copy_if(result.references.begin(), result.references.end(),
                                std::back_inserter(functions), callable);

                            if (!functions.empty()) {
                                std::vector<const builder::Result *> params;

                                if (result.implicit)
                                    params.push_back(result.implicit.get());

                                return callUnpack(call(functions, { params, {} }), result.from);
                            } else {
                                throw VerifyError(result.from, "Reference does not implicitly resolve to anything.");
                            }

                            // if variable, return first
                            // if function, find one with 0 params or infer params, e.g. some
                            // version of match is needed
                        },
                [&](const builder::Result &result) -> builder::Result {
                    auto functionTypename = std::get_if<utils::FunctionTypename>(&result.type);

                    if (functionTypename && functionTypename->kind == utils::FunctionTypename::Kind::Pointer) {
                        throw; // unimplemented
                    }

                    return result;
                } },
            result);
    }

    builder::Result Scope::makeNew(const parser::New *node) {
        auto type = builder.resolveTypename(node->type());

        return builder::Result(builder::Result::FlagTemporary,

            makeMalloc(type),
            utils::ReferenceTypename { std::make_shared<utils::Typename>(type), true, utils::ReferenceKind::Unique },
            &statementContext);
    }

    void Scope::makeAssign(const parser::Assign *node) {
        builder::Result destination = makeExpression(node->children.front()->as<parser::Expression>());

        builder::Result sourceRaw = makeExpression(node->children.back()->as<parser::Expression>());
        std::optional<builder::Result> sourceConverted = convert(sourceRaw, destination.type);

        if (!sourceConverted.has_value()) {
            throw VerifyError(node, "Assignment of type {} to {} is not allowed.", toString(sourceRaw.type),
                toString(destination.type));
        }

        builder::Result source = std::move(*sourceConverted);

        if (!destination.isSet(builder::Result::FlagReference) || !destination.isSet(builder::Result::FlagMutable)) {
            throw VerifyError(node, "Left side of assign expression must be a mutable variable.");
        }

        if (current) {
            llvm::Value *result;

            try {
                if (node->op == parser::Assign::Operator::Assign) {
                    result = get(pass(source));
                } else {
                    auto operation = ([op = node->op]() {
                        switch (op) {
                        case parser::Assign::Operator::Plus:
                            return utils::BinaryOperation::Add;
                        case parser::Assign::Operator::Minus:
                            return utils::BinaryOperation::Sub;
                        case parser::Assign::Operator::Multiply:
                            return utils::BinaryOperation::Mul;
                        case parser::Assign::Operator::Divide:
                            return utils::BinaryOperation::Div;
                        case parser::Assign::Operator::Modulo:
                            return utils::BinaryOperation::Mod;
                        default:
                            throw std::runtime_error("Unimplemented assign node operator.");
                        }
                    })();

                    result = get(pass(combine(destination, source, operation)));
                }
            } catch (const std::runtime_error &e) { throw VerifyError(node, "{}", e.what()); }

            current->CreateStore(result, destination.value);
        }
    }
}
