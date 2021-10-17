#include <builder/operations.h>

#include <builder/handlers.h>

#include <fmt/format.h>

namespace kara::builder::ops {
    namespace nouns {
        builder::Result makeSpecial(const Context &context, utils::SpecialType type) {
            switch (type) { // NOLINT(hicpp-multiway-paths-covered)
            case utils::SpecialType::Null:
                return builder::Result {
                    builder::Result::FlagTemporary,
                    llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(context.builder.context)),
                    utils::PrimitiveTypename { utils::PrimitiveType::Null },
                    context.accumulator,
                };
            default:
                throw;
            }
        }

        builder::Result makeBool(const Context &context, bool value) {
            return builder::Result {
                builder::Result::FlagTemporary,
                llvm::ConstantInt::get(llvm::Type::getInt1Ty(context.builder.context), value),
                utils::PrimitiveTypename { utils::PrimitiveType::Bool },
                context.accumulator,
            };
        }

        builder::Result makeNumber(const Context &context, const utils::NumberValue &value) {
            struct {
                llvm::LLVMContext &context;
                builder::Accumulator *accumulator;

                builder::Result operator()(int64_t s) {
                    return builder::Result {
                        builder::Result::FlagTemporary,
                        llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(context), s),
                        utils::PrimitiveTypename { utils::PrimitiveType::Long },
                        accumulator,
                    };
                }

                builder::Result operator()(uint64_t u) {
                    return builder::Result {
                        builder::Result::FlagTemporary,
                        llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), u),
                        utils::PrimitiveTypename { utils::PrimitiveType::ULong },
                        accumulator,
                    };
                }

                builder::Result operator()(double f) {
                    return builder::Result {
                        builder::Result::FlagTemporary,
                        llvm::ConstantFP::get(llvm::Type::getDoubleTy(context), f),
                        utils::PrimitiveTypename { utils::PrimitiveType::Double },
                        accumulator,
                    };
                }
            } visitor { context.builder.context, context.accumulator };

            return std::visit(visitor, value);
        }

        builder::Result makeString(const Context &context, const std::string &text,
            const std::unordered_map<size_t, builder::Result> &inserts) {
            assert(inserts.empty());

            llvm::Value *ptr = nullptr;

            if (context.ir) {
                llvm::Constant *initial = llvm::ConstantDataArray::getString(context.builder.context, text);

                std::string convertedText(text.size(), '.');

                std::transform(text.begin(), text.end(), convertedText.begin(),
                    [](char c) { return (std::isalpha(c) || std::isdigit(c)) ? c : '_'; });

                auto variable = new llvm::GlobalVariable(*context.builder.module, initial->getType(), true,
                    llvm::GlobalVariable::LinkageTypes::PrivateLinkage, initial, fmt::format("str_{}", convertedText));

                ptr = context.ir->CreateStructGEP(variable, 0);
            }

            return builder::Result {
                builder::Result::FlagTemporary,
                ptr,
                utils::ReferenceTypename {
                    std::make_shared<utils::Typename>(utils::ArrayTypename { utils::ArrayKind::Unbounded,
                        std::make_shared<utils::Typename>(utils::PrimitiveTypename { utils::PrimitiveType::Byte }) }),
                    false },
                context.accumulator,
            };
        }

        builder::Result makeArray(const Context &context, const std::vector<builder::Result> &values) {
            utils::Typename subType = values.empty() ? from(utils::PrimitiveType::Any) : values.front().type;

            if (!std::all_of(values.begin(), values.end(),
                    [&subType](const builder::Result &result) { return result.type == subType; })) {
                die("Array elements must all be the same type {}.", toString(subType));
            }

            utils::ArrayTypename type = {
                utils::ArrayKind::FixedSize,
                std::make_shared<utils::Typename>(subType),
                values.size(),
            };

            llvm::Type *arrayType = context.builder.makeTypename(type);

            llvm::Value *value = nullptr;

            if (context.ir) {
                assert(context.function);

                value = context.function->entry.CreateAlloca(arrayType);

                for (size_t a = 0; a < values.size(); a++) {
                    const builder::Result &result = values[a];

                    llvm::Value *index = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), a);
                    llvm::Value *point = context.ir->CreateInBoundsGEP(
                        value, { llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 0), index });

                    context.ir->CreateStore(ops::get(context, result), point);
                }
            }

            return builder::Result {
                builder::Result::FlagTemporary | builder::Result::FlagReference,
                value,
                type,
                context.accumulator,
            };
        }

        builder::Result makeNew(const Context &context, const utils::Typename &type) {
            auto ptr = ops::makeMalloc(context, type);

            ops::makeInitialize(context, ptr, type);

            return builder::Result {
                builder::Result::FlagTemporary,
                ptr,
                utils::ReferenceTypename {
                    std::make_shared<utils::Typename>(type),
                    true,
                    utils::ReferenceKind::Unique,
                },
                context.accumulator,
            };
        }
    }

    namespace unary {
        builder::Result makeNot(const Context &context, const builder::Result &value) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeNotWithBool,
                           },
                           context, value),
                "Cannot use operator or convert source type {} to bool.", toString(value.type));
        }

        builder::Result makeNegative(const Context &context, const builder::Result &value) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeNegativeWithNumber,
                           },
                           context, value),
                "Cannot use operator or convert source type {} to signed or float.", toString(value.type));
        }

        builder::Result makeReference(const Context &context, const Wrapped &value) {
            // Wrapped
            auto result = handlers::resolve(
                std::array {
                    handlers::makeReferenceWithFunction,
                },
                context, value);

            if (!result) {
                result = handlers::resolve(
                    std::array {
                        handlers::makeReferenceWithVariable,
                    },
                    context, ops::makeInfer(context, value));
            }

            return die(result, "Cannot get reference of temporary.");
        }

        builder::Result makeDereference(const Context &context, const builder::Wrapped &value) {
            auto result = handlers::resolve(
                std::array {
                    handlers::makeDereferenceWithReference,
                    handlers::makeDereferenceWithOptional,
                },
                context, ops::makeInfer(context, value));

            return die(result, "Cannot dereference value of non reference.");
        }
    }

    namespace binary {
        // TODO: Code duplication addressed later...
        builder::Result makeAdd(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeAddNumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeSub(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeSubNumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeMul(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeMulNumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeDiv(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeDivNumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeMod(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeModInt,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeEQ(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeEQNumber,
                               handlers::makeEQRef,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeNE(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeNENumber,
                               handlers::makeNERef,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeGT(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeGTNumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeGE(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeGENumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeLT(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeLTNumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
        builder::Result makeLE(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeLENumber,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }

        builder::Result makeOr(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeOrBool,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }

        builder::Result makeAnd(const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeAndBool,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }


        builder::Result makeFallback(
            const Context &context, const builder::Result &left, const builder::Result &right) {
            return die(handlers::resolve(
                           std::array {
                               handlers::makeFallbackOptional,
                           },
                           context, left, right),
                "Cannot use operator on ls of type {} and rs of type {}.", toString(left.type), toString(right.type));
        }
    }
}
