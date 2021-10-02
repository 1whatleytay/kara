#include <builder/builtins.h>

#include <builder/manager.h>

#include <utils/typename.h>

namespace kara::builder::ops::handlers::builtins {
    bool named(const std::string &input, const std::string &required) { return input.empty() || input == required; }

    namespace arrays {
        Maybe<builder::Result> size(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!array)
                return std::nullopt;

            switch (array->kind) {
            case utils::ArrayKind::UnboundedSized: {
                auto expression = array->expression;

                if (!context.cache)
                    die("Cache required to size field on array.");

                auto cached = context.cache->find(&Cache::expressions, expression);

                if (!cached)
                    die("Attempting to access size of {} but size has not yet been calculated.", toString(value.type));

                return *cached; // may need concert to long
            }

            case utils::ArrayKind::FixedSize:
                return ops::nouns::makeNumber(context, array->size); // uh oh

            case utils::ArrayKind::Unbounded:
                return std::nullopt; // let UFCS maybe take action

            case utils::ArrayKind::Iterable:
                throw;

            case utils::ArrayKind::VariableSize: {
                auto real = makeReal(context, value);
                assert(std::holds_alternative<utils::ArrayTypename>(real.type));

                // mutable should probably be turned off after
                return builder::Result {
                    builder::Result::FlagReference | (real.flags & builder::Result::FlagMutable),
                    context.ir ? context.ir->CreateStructGEP(ops::ref(context, real), 0) : nullptr,
                    utils::PrimitiveTypename { utils::PrimitiveType::ULong },
                    context.accumulator,
                };
            }

            default:
                throw;
            }
        }

        Maybe<builder::Result> capacity(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto real = makeReal(context, value);
            assert(std::holds_alternative<utils::ArrayTypename>(real.type));

            return builder::Result {
                builder::Result::FlagReference | (real.flags & builder::Result::FlagMutable),
                context.ir ? context.ir->CreateStructGEP(ops::ref(context, real), 1) : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::ULong },
                context.accumulator,
            };
        }

        Maybe<builder::Result> data(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto real = makeReal(context, value);
            assert(std::holds_alternative<utils::ArrayTypename>(real.type));

            return builder::Result {
                builder::Result::FlagReference | (real.flags & builder::Result::FlagMutable),
                context.ir ? context.ir->CreateStructGEP(ops::ref(context, real), 2) : nullptr,
                utils::ReferenceTypename {
                    array->value,
                    real.isSet(builder::Result::FlagMutable),
                    utils::ReferenceKind::Regular,
                },
                context.accumulator,
            };
        }

        Maybe<builder::Result> resize(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 2 && named(input[0].first, "array") && named(input[1].first, "size")))
                return std::nullopt;

            auto &value = input[0].second;
            auto &second = input[1].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto arrayResult = ops::makeReal(context, value);

            auto ulongTypename = utils::PrimitiveTypename { utils::PrimitiveType::ULong };

            auto converted = ops::makeConvert(context, second, ulongTypename);
            if (!converted)
                die("Size parameter must be converted to ulong.");

            auto &size = *converted;

            if (context.ir) {
                auto realloc = context.builder.getRealloc();

                auto ptr = ops::ref(context, arrayResult);

                auto sizePtr = context.ir->CreateStructGEP(ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto dataPtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto dataPtrRealType = dataPtr->getType()->getPointerElementType();

                assert(dataPtrRealType->isPointerTy());

                auto dataElementType = dataPtrRealType->getPointerElementType();

                auto dataSize = context.builder.file.manager.target.layout->getTypeAllocSize(dataElementType);
                auto constantSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), dataSize);

                auto dataCasted = context.ir->CreatePointerCast(context.ir->CreateLoad(dataPtr), dataPtrType);

                auto llvmSize = ops::get(context, size);

                auto allocSize = context.ir->CreateMul(llvmSize, constantSize);

                auto newData = context.ir->CreateCall(realloc, { dataCasted, allocSize });
                auto newDataCasted = context.ir->CreatePointerCast(newData, dataPtrRealType);

                context.ir->CreateStore(newDataCasted, dataPtr);
                context.ir->CreateStore(llvmSize, sizePtr);
                context.ir->CreateStore(llvmSize, capacityPtr);
            }

            return builder::Result {
                builder::Result::FlagTemporary,
                nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Nothing },
                nullptr,
            };
        }

        Maybe<builder::Result> reserve(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 2 && named(input[0].first, "array") && named(input[1].first, "size")))
                return std::nullopt;

            auto &value = input[0].second;
            auto &second = input[1].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto arrayResult = ops::makeReal(context, value);

            auto ulongTypename = utils::PrimitiveTypename { utils::PrimitiveType::ULong };

            auto converted = ops::makeConvert(context, second, ulongTypename);
            if (!converted)
                die("Size parameter must be converted to ulong.");

            auto &size = *converted;

            if (context.ir) {
                auto realloc = context.builder.getRealloc();

                auto ptr = ops::ref(context, arrayResult);

                auto sizePtr = context.ir->CreateStructGEP(ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto dataPtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto dataPtrRealType = dataPtr->getType()->getPointerElementType();

                assert(dataPtrRealType->isPointerTy());

                auto dataElementType = dataPtrRealType->getPointerElementType();

                auto dataSize = context.builder.file.manager.target.layout->getTypeAllocSize(dataElementType);
                auto constantSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), dataSize);

                auto dataCasted = context.ir->CreatePointerCast(context.ir->CreateLoad(dataPtr), dataPtrType);

                auto llvmSize = ops::get(context, size);
                auto llvmExistingSize = context.ir->CreateLoad(sizePtr);

                // don't under allocate the elements in our array
                // IDK if it works like that but at least be safe
                auto maximum = context.ir->CreateMaximum(llvmSize, llvmExistingSize);

                auto allocSize = context.ir->CreateMul(maximum, constantSize);

                auto newData = context.ir->CreateCall(realloc, { dataCasted, allocSize });
                auto newDataCasted = context.ir->CreatePointerCast(newData, dataPtrRealType);

                context.ir->CreateStore(newDataCasted, dataPtr);
                context.ir->CreateStore(maximum, capacityPtr);
            }

            return builder::Result {
                builder::Result::FlagTemporary,
                nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Nothing },
                nullptr,
            };
        }

        Maybe<builder::Result> add(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 2 && named(input[0].first, "array") && named(input[1].first, "value")))
                return std::nullopt;

            auto &value = input[0].second;
            auto &second = input[1].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto arrayResult = ops::makeReal(context, value);

            auto converted = ops::makeConvert(context, second, *array->value);
            if (!converted)
                die("Value parameter must be converted to {}.", toString(*array->value));

            auto &toInsert = *converted;

            if (context.ir) {
                auto realloc = context.builder.getRealloc();

                auto ptr = ops::ref(context, arrayResult);

                auto sizePtr = context.ir->CreateStructGEP(ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto dataPtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto dataPtrRealType = dataPtr->getType()->getPointerElementType();

                assert(dataPtrRealType->isPointerTy());

                auto dataElementType = dataPtrRealType->getPointerElementType();

                auto dataSize = context.builder.file.manager.target.layout->getTypeAllocSize(dataElementType);
                auto constantSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), dataSize);

                auto dataCasted = context.ir->CreatePointerCast(context.ir->CreateLoad(dataPtr), dataPtrType);

                // should use vector resizing
                auto one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 1);
                auto originalSize = context.ir->CreateLoad(sizePtr);
                auto llvmSize = context.ir->CreateNUWAdd(originalSize, one);

                auto allocSize = context.ir->CreateMul(llvmSize, constantSize);

                auto newData = context.ir->CreateCall(realloc, { dataCasted, allocSize });
                auto newDataCasted = context.ir->CreatePointerCast(newData, dataPtrRealType);

                auto newElementPtr = context.ir->CreateGEP(newDataCasted, originalSize);
                context.ir->CreateStore(ops::get(context, toInsert), newElementPtr);

                context.ir->CreateStore(newDataCasted, dataPtr);
                context.ir->CreateStore(llvmSize, sizePtr);
                context.ir->CreateStore(llvmSize, capacityPtr); // TODO: vector doubling allocation
            }

            return builder::Result {
                builder::Result::FlagTemporary,
                nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Nothing },
                nullptr,
            };
        }

        Maybe<builder::Result> clear(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto real = makeReal(context, value);
            assert(std::holds_alternative<utils::ArrayTypename>(real.type));

            if (context.ir) {
                auto free = context.builder.getFree();

                auto ptr = ops::ref(context, real);

                auto sizePtr = context.ir->CreateStructGEP(ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto llvmSizeType = llvm::Type::getInt64Ty(context.builder.context);
                auto llvmDataType = dataPtr->getType()->getPointerElementType();

                auto llvmFreeDataType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto llvmFreePointer = context.ir->CreateLoad(dataPtr);
                auto llvmFreeParameter = context.ir->CreatePointerCast(llvmFreePointer, llvmFreeDataType);

                context.ir->CreateCall(free, { llvmFreeParameter });

                assert(llvmDataType->isPointerTy());

                auto pointerType = reinterpret_cast<llvm::PointerType *>(llvmDataType);

                auto llvmZero = llvm::ConstantInt::get(llvmSizeType, 0);
                auto llvmNull = llvm::ConstantPointerNull::get(pointerType);

                context.ir->CreateStore(llvmZero, sizePtr);
                context.ir->CreateStore(llvmZero, capacityPtr);
                context.ir->CreateStore(llvmNull, dataPtr);
            }

            return builder::Result {
                builder::Result::FlagTemporary,
                nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Nothing },
                nullptr,
            };
        }

        Maybe<builder::Result> array(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            assert(value.isSet(builder::Result::FlagTemporary)); // sanity check

            auto reference = std::get_if<utils::ReferenceTypename>(&value.type);
            if (!(reference && reference->kind == utils::ReferenceKind::Unique))
                return std::nullopt;

            auto array = std::get_if<utils::ArrayTypename>(&*reference->value);
            auto arrayIsUsable = [&]() {
                return array->kind == utils::ArrayKind::UnboundedSized || array->kind == utils::ArrayKind::FixedSize;
            };
            if (!(array && arrayIsUsable()))
                return std::nullopt;

            utils::ArrayTypename resultType {
                utils::ArrayKind::VariableSize,
                array->value,
            };

            auto llvmValue = ops::makeAlloca(context, resultType);

            if (context.ir) {
                auto sizePtr = context.ir->CreateStructGEP(llvmValue, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(llvmValue, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(llvmValue, 2); // 2 is data

                assert(context.cache);

                llvm::Value *size;
                llvm::Value *parameter;

                switch (array->kind) {
                case utils::ArrayKind::FixedSize: {
                    auto sizeType = llvm::Type::getInt64Ty(context.builder.context);

                    auto zero = llvm::ConstantInt::get(sizeType, 0);

                    size = llvm::ConstantInt::get(sizeType, array->size);
                    parameter = context.ir->CreateGEP(ops::get(context, value), { zero, zero });
                    break;
                }

                case utils::ArrayKind::UnboundedSized: {
                    assert(array->expression);

                    auto cached = context.cache->find(&Cache::expressions, array->expression);
                    if (!cached)
                        die("Cached expression is not computed.");

                    size = ops::get(context, *cached);
                    parameter = ops::get(context, value);

                    break;
                }

                default:
                    throw;
                }

                // it shouldn't delete the unique ptr cuz makePass I dont think
                context.ir->CreateStore(size, sizePtr);
                context.ir->CreateStore(size, capacityPtr);
                context.ir->CreateStore(parameter, dataPtr);
            }

            return builder::Result {
                builder::Result::FlagTemporary | builder::Result::FlagReference,
                llvmValue,
                resultType,
                nullptr,
            };
        }
    }

    std::vector<BuiltinFunction> matching(const std::string &name) {
        std::vector<BuiltinFunction> result;

        for (const auto &pair : functions) {
            if (pair.first == name) {
                result.push_back(pair.second);
            }
        }

        return result;
    }
}