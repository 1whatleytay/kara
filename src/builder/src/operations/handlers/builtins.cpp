#include <builder/builtins.h>

#include <builder/target.h>
#include <builder/manager.h>

#include <utils/typename.h>

#include <cassert>

namespace kara::builder::ops::handlers::builtins {
    bool named(const std::string &input, const std::string &required) { return input.empty() || input == required; }

    namespace arrays {
        std::optional<std::tuple<builder::Result, const utils::ArrayTypename *>> popArray(
            const ops::Context &context,
            ops::matching::MatchInputFlattened &input,
            size_t index = 0, const char *name = "array") {
            if (!named(input[index].first, "array"))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findRealType(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!array)
                return std::nullopt;

            return std::make_tuple(ops::makeRealType(context, value), array);
        }

        Maybe<builder::Result> size(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            if (input.size() != 1)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

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
                auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

                // mutable should probably be turned off after
                return builder::Result {
                    builder::Result::FlagReference | (value.flags & builder::Result::FlagMutable),
                    context.ir ? context.ir->CreateStructGEP(arrayStructType, ops::ref(context, value), 0) : nullptr,
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

            if (input.size() != 1)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            if (array->kind != utils::ArrayKind::VariableSize)
                return std::nullopt;

            auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

            return builder::Result {
                builder::Result::FlagReference | (value.flags & builder::Result::FlagMutable),
                context.ir ? context.ir->CreateStructGEP(arrayStructType, ops::ref(context, value), 1) : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::ULong },
                context.accumulator,
            };
        }

        Maybe<builder::Result> data(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            if (input.size() != 1)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            if (array->kind != utils::ArrayKind::VariableSize)
                return std::nullopt;

            auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

            return builder::Result {
                builder::Result::FlagReference | (value.flags & builder::Result::FlagMutable),
                context.ir ? context.ir->CreateStructGEP(arrayStructType, ops::ref(context, value), 2) : nullptr,
                utils::ReferenceTypename {
                    array->value,
                    value.isSet(builder::Result::FlagMutable),
                    utils::ReferenceKind::Regular,
                },
                context.accumulator,
            };
        }

        Maybe<builder::Result> resize(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            if (input.size() != 2)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            if (!named(input[1].first, "size"))
                return std::nullopt;

            if (array->kind != utils::ArrayKind::VariableSize)
                return std::nullopt;

            auto ulongTypename = utils::PrimitiveTypename { utils::PrimitiveType::ULong };

            auto converted = ops::makeConvert(context, input[1].second, ulongTypename);
            if (!converted)
                die("Size parameter must be converted to ulong.");

            auto &size = *converted;

            if (context.ir) {
                auto reallocFunc = context.builder.getRealloc();

                auto ptr = ops::ref(context, value);

                auto baseType = context.builder.makeTypename(*array->value);
                auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

                auto sizePtr = context.ir->CreateStructGEP(arrayStructType, ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto dataPtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto dataPtrRealType = dataPtr->getType()->getPointerElementType();

                assert(dataPtrRealType->isPointerTy());

                auto dataElementType = dataPtrRealType->getPointerElementType();

                auto dataSize = context.builder.target.layout->getTypeAllocSize(dataElementType);
                auto constantSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), dataSize);

                auto baseTypePointer = llvm::PointerType::get(baseType, 0);

                auto dataPtrLoaded = context.ir->CreateLoad(baseTypePointer, dataPtr);
                auto dataCasted = context.ir->CreatePointerCast(dataPtrLoaded, dataPtrType);

                auto llvmSize = ops::get(context, size);

                auto allocSize = context.ir->CreateMul(llvmSize, constantSize);

                auto newData = context.ir->CreateCall(reallocFunc, { dataCasted, allocSize });
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

            if (input.size() != 2)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            if (!named(input[1].first, "size"))
                return std::nullopt;

            if (array->kind != utils::ArrayKind::VariableSize)
                return std::nullopt;

            auto ulongTypename = utils::PrimitiveTypename { utils::PrimitiveType::ULong };

            auto converted = ops::makeConvert(context, input[1].second, ulongTypename);
            if (!converted)
                die("Size parameter must be converted to ulong.");

            auto &size = *converted;

            if (context.ir) {
                auto reallocFunc = context.builder.getRealloc();

                auto baseType = context.builder.makeTypename(*array->value);
                auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

                auto ptr = ops::ref(context, value);

                auto sizePtr = context.ir->CreateStructGEP(arrayStructType, ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto dataPtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto dataPtrRealType = dataPtr->getType()->getPointerElementType();

                assert(dataPtrRealType->isPointerTy());

                auto dataElementType = dataPtrRealType->getPointerElementType();

                auto dataSize = context.builder.target.layout->getTypeAllocSize(dataElementType);
                auto constantSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), dataSize);

                auto baseTypePointer = llvm::PointerType::get(baseType, 0);

                auto dataCasted
                    = context.ir->CreatePointerCast(context.ir->CreateLoad(baseTypePointer, dataPtr), dataPtrType);

                auto i64 = llvm::Type::getInt64Ty(context.builder.context);

                auto llvmSize = ops::get(context, size);
                auto llvmExistingSize = context.ir->CreateLoad(i64, sizePtr);

                // don't under allocate the elements in our array
                // IDK if it works like that but at least be safe
                auto maximum = context.ir->CreateMaximum(llvmSize, llvmExistingSize);

                auto allocSize = context.ir->CreateMul(maximum, constantSize);

                auto newData = context.ir->CreateCall(reallocFunc, { dataCasted, allocSize });
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

            if (input.size() != 2)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            if (!named(input[1].first, "value"))
                return std::nullopt;

            if (array->kind != utils::ArrayKind::VariableSize)
                return std::nullopt;

            auto converted = ops::makeConvert(context, input[1].second, *array->value);
            if (!converted) {
                die("`add` builtin's value parameter could not be converted to array element type {}.",
                    toString(*array->value));
            }

            auto &toInsert = *converted;

            if (context.ir) {
                auto reallocFunc = context.builder.getRealloc();

                auto ptr = ops::ref(context, value);

                auto baseType = context.builder.makeTypename(*array->value);
                auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

                auto sizePtr = context.ir->CreateStructGEP(arrayStructType, ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto dataPtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto dataPtrRealType = dataPtr->getType()->getPointerElementType();

                assert(dataPtrRealType->isPointerTy());

                auto dataElementType = dataPtrRealType->getPointerElementType();

                auto dataSize = context.builder.target.layout->getTypeAllocSize(dataElementType);
                auto constantSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), dataSize);

                auto baseTypePointer = llvm::PointerType::get(baseType, 0);

                auto dataCasted
                    = context.ir->CreatePointerCast(context.ir->CreateLoad(baseTypePointer, dataPtr), dataPtrType);

                // should use vector resizing
                auto i64 = llvm::Type::getInt64Ty(context.builder.context);

                auto one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 1);
                auto originalSize = context.ir->CreateLoad(i64, sizePtr);
                auto llvmSize = context.ir->CreateNUWAdd(originalSize, one);

                auto allocSize = context.ir->CreateMul(llvmSize, constantSize);

                auto newData = context.ir->CreateCall(reallocFunc, { dataCasted, allocSize });
                auto newDataCasted = context.ir->CreatePointerCast(newData, dataPtrRealType);

                auto newElementPtr = context.ir->CreateGEP(baseType, newDataCasted, originalSize);
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

            if (input.size() != 1)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            if (array->kind != utils::ArrayKind::VariableSize)
                return std::nullopt;

            if (context.ir) {
                auto freeFunc = context.builder.getFree();

                auto ptr = ops::ref(context, value);

                auto baseType = context.builder.makeTypename(*array->value);
                auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

                auto sizePtr = context.ir->CreateStructGEP(arrayStructType, ptr, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(arrayStructType, ptr, 2); // 2 is data

                assert(dataPtr->getType()->isPointerTy());

                auto llvmSizeType = llvm::Type::getInt64Ty(context.builder.context);
                auto llvmDataType = dataPtr->getType()->getPointerElementType();

                auto baseTypePointer = llvm::PointerType::get(baseType, 0);

                auto llvmFreeDataType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto llvmFreePointer = context.ir->CreateLoad(baseTypePointer, dataPtr);
                auto llvmFreeParameter = context.ir->CreatePointerCast(llvmFreePointer, llvmFreeDataType);

                context.ir->CreateCall(freeFunc, { llvmFreeParameter });

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

            auto value = ops::makePass(context, input[0].second);

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
                auto baseType = context.builder.makeTypename(*array->value);
                auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

                auto sizePtr = context.ir->CreateStructGEP(arrayStructType, llvmValue, 0); // 0 is size
                auto capacityPtr = context.ir->CreateStructGEP(arrayStructType, llvmValue, 1); // 1 is capacity
                auto dataPtr = context.ir->CreateStructGEP(arrayStructType, llvmValue, 2); // 2 is data

                assert(context.cache);

                llvm::Value *size;
                llvm::Value *parameter;

                switch (array->kind) {
                case utils::ArrayKind::FixedSize: {
                    auto valueType = context.builder.makeTypename(*array);

                    auto sizeType = llvm::Type::getInt64Ty(context.builder.context);

                    auto zero = llvm::ConstantInt::get(sizeType, 0);

                    size = llvm::ConstantInt::get(sizeType, array->size);
                    parameter = context.ir->CreateGEP(valueType, ops::get(context, value), { zero, zero }); // ?
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

        Maybe<builder::Result> first(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            if (input.size() != 1)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            auto zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 0);

            return ops::modifiers::makeIndexRaw(context, value, zero);
        }

        Maybe<builder::Result> last(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            if (input.size() != 1)
                return std::nullopt;

            auto arrayValue = popArray(context, input);
            if (!arrayValue)
                return std::nullopt;

            auto [value, array] = *arrayValue;

            switch (array->kind) {
            case utils::ArrayKind::UnboundedSized: {
                auto expression = array->expression;

                if (!context.cache)
                    die("Cache required to size field on array.");

                auto cached = context.cache->find(&Cache::expressions, expression);

                if (!cached)
                    die("Attempting to access size of {} but size has not yet been calculated.", toString(value.type));

                auto index = ops::get(context, *cached);

                if (context.ir) {
                    auto one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 1);

                    index = context.ir->CreateSub(index, one);
                }

                return ops::modifiers::makeIndexRaw(context, value, index);
            }

            case utils::ArrayKind::FixedSize: {
                auto index = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), array->size - 1);

                return ops::modifiers::makeIndexRaw(context, value, index);
            }

            case utils::ArrayKind::Unbounded:
                return std::nullopt; // let UFCS maybe take action

            case utils::ArrayKind::Iterable:
                throw;

            case utils::ArrayKind::VariableSize: {
                auto arrayStructType = context.builder.makeVariableArrayType(*array->value);

                llvm::Value *index = nullptr;

                if (context.ir) {
                    auto i64 = llvm::Type::getInt64Ty(context.builder.context);
                    auto sizePtr = context.ir->CreateStructGEP(arrayStructType, ops::ref(context, value), 0);
                    auto size = context.ir->CreateLoad(i64, sizePtr);

                    auto one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 1);

                    index = context.ir->CreateSub(size, one);
                }

                return ops::modifiers::makeIndexRaw(context, value, index);
            }

            default:
                throw;
            }
        }
    }

    namespace misc {
        Maybe<builder::Result> byteSize(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            if (!named(input[0].first, "value"))
                return std::nullopt;

            auto &value = input[0].second;

            auto llvmType = context.builder.makeTypename(value.type);
            auto byteSize = context.builder.target.layout->getTypeAllocSize(llvmType);

            return ops::nouns::makeNumber(context, static_cast<uint64_t>(byteSize));
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