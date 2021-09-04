#include <builder/handlers.h>

namespace kara::builder::ops::handlers {
    Maybe<utils::Typename> negotiateEqual(const utils::Typename &left, const utils::Typename &right) {
        if (left != right)
            return std::nullopt;

        return left;
    }

    Maybe<utils::Typename> negotiatePrimitive(const utils::Typename &left, const utils::Typename &right) {
        auto leftPrim = std::get_if<utils::PrimitiveTypename>(&left);
        auto rightPrim = std::get_if<utils::PrimitiveTypename>(&right);

        if (!(leftPrim && rightPrim && leftPrim->isNumber() && rightPrim->isNumber()))
            return std::nullopt;

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

        return from(type);
    }

    const utils::PrimitiveTypename *asPrim(const utils::Typename &type) {
        return std::get_if<utils::PrimitiveTypename>(&type);
    }

    const utils::PrimitiveTypename *asPrimTo(const utils::Typename &type, utils::PrimitiveType target) {
        auto v = asPrim(type);

        if (v && v->type == target)
            return v;

        return nullptr;
    }

    const utils::ReferenceTypename *asRef(const utils::Typename &type) {
        return std::get_if<utils::ReferenceTypename>(&type);
    }

    const utils::ReferenceTypename *asRefTo(const utils::Typename &type, const utils::Typename &target) {
        auto v = asRef(type);

        if (v && *v->value == target)
            return v;

        return nullptr;
    }

    template <typename T>
    using ConversionResult = std::optional<std::tuple<builder::Result, builder::Result, T>>;

    using BinaryConversion = ConversionResult<utils::PrimitiveTypename>;
    using NumberConversion = ConversionResult<utils::PrimitiveTypename>;
    using ReferenceConversion = ConversionResult<utils::ReferenceTypename>;

    NumberConversion toNumber(const Context &context, const builder::Result &left, const builder::Result &right) {
        auto leftPrim = asPrim(left.type);
        auto rightPrim = asPrim(right.type);

        if (!(leftPrim && rightPrim && leftPrim->isNumber() && rightPrim->isNumber()))
            return std::nullopt;

        // important we die after because ops::makeConvertExplicit might have generated code?
        auto converted = ops::makeConvertDouble(context, left, right);
        if (!converted)
            die("Cannot convert two number types to each other.");

        auto [a, b] = *converted;

        auto aPrim = asPrim(a.type);
        auto bPrim = asPrim(b.type);

        assert(aPrim && bPrim && *aPrim == *bPrim);

        return std::make_tuple(a, b, *aPrim);
    }

    ReferenceConversion toReference(const Context &context, const builder::Result &left, const builder::Result &right) {
        auto leftRef = asRef(left.type);
        auto rightRef = asRef(right.type);

        if (!(leftRef && rightRef))
            return std::nullopt;

        auto converted = ops::makeConvertDouble(context, left, right);
        if (!converted)
            die("Cannot convert references to each other normally.");

        auto [a, b] = *converted;

        auto aRef = asRef(a.type);
        auto bRef = asRef(b.type);

        assert(aRef && bRef && *aRef == *bRef);

        return std::make_tuple(a, b, *aRef);
    }

    BinaryConversion toBinary(const Context &context, const builder::Result &left, const builder::Result &right) {
        auto typenameBool = utils::PrimitiveTypename { utils::PrimitiveType::Bool };

        auto a = ops::makeConvert(context, left, typenameBool);
        auto b = ops::makeConvert(context, right, typenameBool);

        if (!(a && b))
            return std::nullopt;

        return std::make_tuple(*a, *b, typenameBool);
    }

    using SimpleBaseImpl = llvm::Value *(*)(llvm::IRBuilder<> &builder, llvm::Value *, llvm::Value *);
    using PrimitiveBaseImpl
        = llvm::Value *(*)(llvm::IRBuilder<> &builder, llvm::Value *, llvm::Value *, const utils::PrimitiveTypename &);

    Maybe<builder::Result> handlerNumberToNumberBase(
        const Context &context, const builder::Result &left, const builder::Result &right, PrimitiveBaseImpl f) {
        auto numbers = toNumber(context, left, right);
        if (!numbers)
            return std::nullopt;

        auto [a, b, prim] = *numbers;

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? f(*context.ir, ops::get(context, a), ops::get(context, b), prim) : nullptr,
            a.type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> handlerNumberToBoolBase(
        const Context &context, const builder::Result &left, const builder::Result &right, PrimitiveBaseImpl f) {
        auto numbers = toNumber(context, left, right);
        if (!numbers)
            return std::nullopt;

        auto [a, b, prim] = *numbers;

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? f(*context.ir, ops::get(context, a), ops::get(context, b), prim) : nullptr,
            utils::PrimitiveTypename { utils::PrimitiveType::Bool },
            context.accumulator,
        };
    }

    Maybe<builder::Result> handlerReferenceToBoolBase(
        const Context &context, const builder::Result &left, const builder::Result &right, SimpleBaseImpl f) {
        auto references = toReference(context, left, right);
        if (!references)
            return std::nullopt;

        auto [a, b, type] = *references;

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? f(*context.ir, ops::get(context, a), ops::get(context, b)) : nullptr,
            utils::PrimitiveTypename { utils::PrimitiveType::Bool },
            context.accumulator,
        };
    }

    Maybe<builder::Result> handlerBooleanBase(
        const Context &context, const builder::Result &left, const builder::Result &right, SimpleBaseImpl f) {
        auto bin = toBinary(context, left, right);
        if (!bin)
            return std::nullopt;

        auto [a, b, prim] = *bin;

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? f(*context.ir, ops::get(context, a), ops::get(context, b)) : nullptr,
            utils::PrimitiveTypename { utils::PrimitiveType::Bool },
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertBridgeImplicitReference(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typeRef = asRef(type);
        auto resultRef = asRef(result.type);

        auto workable = [typeRef, &result]() {
            auto toRegular = typeRef->kind == utils::ReferenceKind::Regular;
            auto mutabilityOk = typeRef->isMutable && !result.isSet(builder::Result::FlagMutable);

            return toRegular && mutabilityOk;
        };

        auto typePointsTo = typeRef && *typeRef->value == result.type;
        auto typeIsRefAndNotResult = typeRef && !resultRef;

        if (!((typePointsTo || typeIsRefAndNotResult) && workable()))
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagTemporary,
            ops::ref(context, result),
            utils::ReferenceTypename {
                std::make_shared<utils::Typename>(result.type),
                result.isSet(builder::Result::FlagMutable),
                utils::ReferenceKind::Regular,
            },
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertBridgeImplicitDereference(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        //        auto typeRef = asRef(type);
        auto resultRef = asRef(result.type);

        auto resultPointsTo = resultRef && *resultRef->value == type;
        //        auto resultIsRefAndNotType = resultRef && !typeRef;

        if (!(resultPointsTo /* || resultIsRefAndNotType*/))
            return std::nullopt;

        if (!resultRef) // for intellisense mostly
            throw;

        return builder::Result {
            builder::Result::FlagReference | (resultRef->isMutable ? builder::Result::FlagMutable : 0),
            ops::get(context, result),
            *resultRef->value,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertEqual(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        if (result.type != type)
            return std::nullopt;

        return result;
    }

    Maybe<builder::Result> makeConvertForcedRefToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force) {
        if (!force || !asRef(type) || !asRef(result.type))
            return std::nullopt;

        auto llvmType = context.builder.makeTypename(type);

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreateBitCast(ops::get(context, result), llvmType) : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertForcedULongToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force) {
        if (!force || !asPrimTo(type, utils::PrimitiveType::ULong) || !asRef(result.type))
            return std::nullopt;

        auto llvmType = context.builder.makeTypename(type);

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreatePtrToInt(ops::get(context, result), llvmType) : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertForcedRefToULong(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force) {
        if (!force || !asPrimTo(result.type, utils::PrimitiveType::ULong) || !asRef(type))
            return std::nullopt;

        auto llvmType = context.builder.makeTypename(type);

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreateIntToPtr(ops::get(context, result), llvmType) : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertUniqueOrMutableToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typeRef = asRef(type);
        auto resultRef = asRef(result.type);

        auto check = [typeRef, resultRef]() {
            auto validConvert
                = typeRef->kind == utils::ReferenceKind::Regular && resultRef->kind != utils::ReferenceKind::Shared;
            auto mutabilityOk = !typeRef->isMutable || resultRef->isMutable;

            return validConvert && mutabilityOk;
        };

        if (!(typeRef && resultRef && *typeRef->value == *resultRef->value) || !check())
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagTemporary,
            ops::get(context, result),
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertRefToAnyRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typeRef = asRefTo(type, from(utils::PrimitiveType::Any));
        auto resultRef = asRef(result.type);

        if (!(typeRef && resultRef && (!typeRef->isMutable || resultRef->isMutable)))
            return std::nullopt;

        auto llvmType = context.builder.makeTypename(type);

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreatePointerCast(ops::get(context, result), llvmType) : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertRefToUnboundedRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typeRef = asRef(type);
        auto resultRef = asRef(result.type);

        auto working = [typeRef, resultRef]() {
            auto array = std::get_if<utils::ArrayTypename>(typeRef->value.get());

            return array && array->kind == utils::ArrayKind::Unbounded && *array->value == *resultRef->value;
        };

        if (!(typeRef && resultRef && working()))
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagTemporary,
            ops::get(context, result),
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertFixedRefToUnboundedRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typeRef = asRef(type);
        auto resultRef = asRef(result.type);

        auto working = [typeRef, resultRef]() {
            auto typeArray = std::get_if<utils::ArrayTypename>(typeRef->value.get());
            auto resultArray = std::get_if<utils::ArrayTypename>(resultRef->value.get());

            return typeArray && resultArray && typeArray->kind == utils::ArrayKind::Unbounded
                && resultArray->kind == utils::ArrayKind::FixedSize && *typeArray->value == *resultArray->value;
        };

        if (!(typeRef && resultRef && working()))
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreateStructGEP(ops::get(context, result), 0) : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertNullToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        if (!(asPrimTo(result.type, utils::PrimitiveType::Null) && asRef(type)))
            return std::nullopt;

        auto llvmType = context.builder.makeTypename(type);

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreatePointerCast(ops::get(context, result), llvmType) : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertRefToBool(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        if (!(asRef(result.type) && asPrimTo(type, utils::PrimitiveType::Bool)))
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreateIsNotNull(ops::get(context, result)) : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertIntToFloat(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typePrim = asPrim(type);
        auto resultPrim = asPrim(result.type);

        if (!(typePrim && resultPrim && resultPrim->isInteger() && typePrim->isFloat()))
            return std::nullopt;

        auto llvmType = context.builder.makePrimitiveType(typePrim->type);

        auto make = [&result, resultPrim, &context, llvmType]() {
            if (resultPrim->isSigned())
                return context.ir->CreateSIToFP(ops::get(context, result), llvmType);

            return context.ir->CreateUIToFP(ops::get(context, result), llvmType);
        };

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? make() : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertFloatToInt(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typePrim = asPrim(type);
        auto resultPrim = asPrim(result.type);

        if (!(typePrim && resultPrim && resultPrim->isFloat() && typePrim->isInteger()))
            return std::nullopt;

        auto llvmType = context.builder.makePrimitiveType(typePrim->type);

        auto make = [&result, typePrim, &context, llvmType]() {
            auto value = ops::get(context, result);

            if (typePrim->isSigned())
                return context.ir->CreateFPToSI(value, llvmType);

            return context.ir->CreateFPToUI(value, llvmType);
        };

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? make() : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeConvertPrimitiveExtend(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool) {
        auto typePrim = asPrim(type);
        auto resultPrim = asPrim(result.type);

        if (!(typePrim && resultPrim && typePrim->isFloat() == resultPrim->isFloat()))
            return std::nullopt;

        auto needsTrunc = resultPrim->priority() > typePrim->priority();
        auto llvmType = context.builder.makePrimitiveType(typePrim->type);

        auto make = [needsTrunc, &result, typePrim, &context, llvmType]() {
            auto value = ops::get(context, result);

            if (typePrim->isFloat()) {
                if (needsTrunc)
                    return context.ir->CreateFPTrunc(value, llvmType);
                else
                    return context.ir->CreateFPExt(value, llvmType);
            } else {
                if (typePrim->isSigned())
                    return context.ir->CreateSExtOrTrunc(value, llvmType);
                else
                    return context.ir->CreateZExtOrTrunc(value, llvmType);
            }
        };

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? make() : nullptr,
            type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeNotWithBool(const Context &context, const builder::Result &value) {
        auto typenameBool = utils::PrimitiveTypename { utils::PrimitiveType::Bool };

        auto converted = ops::makeConvert(context, value, typenameBool);
        if (!converted)
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? context.ir->CreateNot(ops::get(context, *converted)) : nullptr,
            utils::PrimitiveTypename { utils::PrimitiveType::Bool },
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeNegativeWithNumber(const Context &context, const builder::Result &value) {
        auto typePrim = std::get_if<utils::PrimitiveTypename>(&value.type);

        if (!typePrim || !(typePrim->isSigned() || typePrim->isFloat()))
            return std::nullopt;

        auto make = [&context, &value, typePrim]() {
            if (typePrim->isFloat())
                return context.ir->CreateFNeg(ops::get(context, value));
            else
                return context.ir->CreateNeg(ops::get(context, value));
        };

        return builder::Result {
            builder::Result::FlagTemporary,
            context.ir ? make() : nullptr,
            value.type,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeReferenceWithVariable(const Context &context, const builder::Result &value) {
        if (!value.isSet(builder::Result::FlagReference))
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagTemporary,
            value.value,
            utils::ReferenceTypename {
                std::make_shared<utils::Typename>(value.type),
            },
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeDereferenceWithReference(const Context &context, const builder::Result &value) {
        auto refType = std::get_if<utils::ReferenceTypename>(&value.type);

        if (!refType)
            return std::nullopt;

        return builder::Result {
            builder::Result::FlagReference | (refType->isMutable ? builder::Result::FlagMutable : 0),
            ops::get(context, value),
            *refType->value,
            context.accumulator,
        };
    }

    Maybe<builder::Result> makeAddNumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToNumberBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFAdd(a, b);
            else
                return ir.CreateAdd(a, b);
        });
    }

    Maybe<builder::Result> makeSubNumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToNumberBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFSub(a, b);
            else
                return ir.CreateSub(a, b);
        });
    }

    Maybe<builder::Result> makeMulNumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToNumberBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFMul(a, b);
            else
                return ir.CreateMul(a, b);
        });
    }

    Maybe<builder::Result> makeDivNumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToNumberBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFDiv(a, b);
            else if (prim.isSigned())
                return ir.CreateSDiv(a, b);
            else
                return ir.CreateUDiv(a, b);
        });
    }

    Maybe<builder::Result> makeModInt(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToNumberBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFRem(a, b);
            else if (prim.isSigned())
                return ir.CreateSRem(a, b);
            else
                return ir.CreateURem(a, b);
        });
    }

    Maybe<builder::Result> makeEQNumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToBoolBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFCmpOEQ(a, b);
            else
                return ir.CreateICmpEQ(a, b);
        });
    }

    Maybe<builder::Result> makeEQRef(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerReferenceToBoolBase(
            context, left, right, [](auto &ir, auto a, auto b) { return ir.CreateICmpEQ(a, b); });
    }

    Maybe<builder::Result> makeNENumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToBoolBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFCmpONE(a, b);
            else
                return ir.CreateICmpNE(a, b);
        });
    }

    Maybe<builder::Result> makeNERef(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerReferenceToBoolBase(
            context, left, right, [](auto &ir, auto a, auto b) { return ir.CreateICmpNE(a, b); });
    }

    Maybe<builder::Result> makeGTNumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToBoolBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFCmpOGT(a, b);
            else if (prim.isSigned())
                return ir.CreateICmpSGT(a, b);
            else
                return ir.CreateICmpUGT(a, b);
        });
    }

    Maybe<builder::Result> makeGENumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToBoolBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFCmpOGE(a, b);
            else if (prim.isSigned())
                return ir.CreateICmpSGE(a, b);
            else
                return ir.CreateICmpUGE(a, b);
        });
    }

    Maybe<builder::Result> makeLTNumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToBoolBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFCmpOLT(a, b);
            else if (prim.isSigned())
                return ir.CreateICmpSLT(a, b);
            else
                return ir.CreateICmpULT(a, b);
        });
    }

    Maybe<builder::Result> makeLENumber(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerNumberToBoolBase(context, left, right, [](auto &ir, auto a, auto b, auto &prim) {
            if (prim.isFloat())
                return ir.CreateFCmpOLE(a, b);
            else if (prim.isSigned())
                return ir.CreateICmpSLE(a, b);
            else
                return ir.CreateICmpULE(a, b);
        });
    }

    Maybe<builder::Result> makeOrBool(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerBooleanBase(context, left, right, [](auto &ir, auto a, auto b) { return ir.CreateOr(a, b); });
    }

    Maybe<builder::Result> makeAndBool(
        const Context &context, const builder::Result &left, const builder::Result &right) {
        return handlerBooleanBase(context, left, right, [](auto &ir, auto a, auto b) { return ir.CreateAnd(a, b); });
    }
}