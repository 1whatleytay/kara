#include <builder/platform.h>

#include <builder/target.h>
#include <builder/operations.h>

#include <llvm/ADT/Triple.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>

#include <cassert>

namespace kara::builder {
    std::vector<llvm::Type *> FormatArgumentsPackage::parameterTypes() const {
        std::vector<llvm::Type *> result;
        result.reserve(parameters.size());

        for (const auto &p : parameters)
            result.push_back(p.second);

        return result;
    }

    std::vector<llvm::Type *> FormatArgumentsResult::parameterTypes() const {
        std::vector<llvm::Type *> result;
        result.reserve(parameters.size());

        for (const auto &[_1, type, _2] : parameters)
            result.push_back(type);

        return result;
    }

    std::unique_ptr<Platform> Platform::byNative() {
        return Platform::byTriple(llvm::sys::getDefaultTargetTriple());
    }

    std::unique_ptr<Platform> Platform::byTriple(const std::string &name) {
        llvm::Triple triple(name);

        switch (triple.getOS()) {
        case llvm::Triple::Darwin:
        case llvm::Triple::FreeBSD:
        case llvm::Triple::IOS:
        case llvm::Triple::KFreeBSD:
        case llvm::Triple::Linux:
        case llvm::Triple::MacOSX:
        case llvm::Triple::NetBSD:
        case llvm::Triple::OpenBSD:
        case llvm::Triple::Solaris:
        case llvm::Triple::TvOS:
        case llvm::Triple::WatchOS:
            return std::make_unique<SysVPlatform>();

        default:
            return std::make_unique<Platform>();
        }
    }

    FormatArgumentsResult Platform::formatArguments(
        const Target &target,
        const FormatArgumentsPackage &package) {

        FormatArgumentsResult result = { package.returnType };
        result.parameters.reserve(package.parameters.size());

        for (const auto &[name, type] : package.parameters) {
            result.parameters.emplace_back(name, type, llvm::AttrBuilder());
        }

        return result;
    }

    llvm::Value *Platform::invokeFunction(
        const ops::Context &context,
        llvm::FunctionCallee function,
        llvm::Type *returnType,
        const std::vector<llvm::Value *> &values) {
        if (context.ir) {
            return context.ir->CreateCall(function, values);
        }

        return nullptr;
    }

    std::vector<llvm::Value *> Platform::tieArguments(
        const ops::Context &context,
        llvm::Type *returnType,
        const std::vector<llvm::Type *> &argumentTypes,
        const std::vector<llvm::Argument *> &arguments) {
        std::vector<llvm::Value *> values;
        values.reserve(arguments.size());

        for (auto arg : arguments)
            values.push_back(arg);

        return values;
    }

    void Platform::tieReturn(
        const ops::Context &context,
        llvm::Type *returnType,
        llvm::Value *value,
        const std::vector<llvm::Argument *> &arguments) {
        assert(context.ir);

        if (returnType->isVoidTy()) {
            assert(!value);
            context.ir->CreateRetVoid();
        } else {
            context.ir->CreateRet(value);
        }
    }

    std::optional<std::vector<llvm::Type *>> flattenLLVMType(llvm::Type *type) {
        // make sure array isnt something giant, check llvm type size before calling

        std::vector<llvm::Type *> result;

        if (type->isIntegerTy() || type->isFloatTy() || type->isDoubleTy() || type->isPointerTy()) {
            result.push_back(type);
        } else if (type->isStructTy()) {
            auto structType = reinterpret_cast<llvm::StructType *>(type);

            for (auto element : structType->elements()) {
                auto flattened = flattenLLVMType(element);

                if (!flattened)
                    return std::nullopt;

                result.insert(result.end(), flattened->begin(), flattened->end());
            }
        } else if (type->isArrayTy()) {
            auto arrayType = reinterpret_cast<llvm::ArrayType *>(type);

            auto elementType = arrayType->getElementType();
            auto numElements = arrayType->getNumElements();

            auto base = flattenLLVMType(elementType);

            if (!base)
                return std::nullopt;

            result.reserve(result.size() + numElements * base->size());

            for (size_t a = 0; a < numElements; a++)
                result.insert(result.end(), base->begin(), base->end()); // insert many?
        } else {
            return std::nullopt;
        }

        assert(!result.empty());
        assert(std::all_of(result.begin(), result.end(), [](auto type) {
            return type->isIntegerTy() || type->isFloatTy() || type->isDoubleTy() || type->isPointerTy();
        }));

        return result;
    }

    std::optional<std::vector<llvm::Type *>> combineSysVLLVMTypes(
        const builder::Target &target, const std::vector<llvm::Type *> &types) {
        assert(!types.empty());

        std::vector<llvm::Type *> result;

        size_t bytesSoFar = 0;
        bool allFloats = true;
        bool allDoubles = true;

        constexpr size_t dwordSize = 8;
        constexpr size_t maxTypeCount = 2;

        auto push = [&]() {
            assert(bytesSoFar > 0);

            if (allFloats) {
                assert(bytesSoFar % 4 == 0);

                size_t numFloats = bytesSoFar / 4;

                llvm::Type *baseType = llvm::Type::getFloatTy(*target.context);

                if (numFloats > 1)
                    baseType = llvm::VectorType::get(baseType, numFloats, false);

                result.push_back(baseType);
            } else if (allDoubles) {
                assert(bytesSoFar == 8);

                result.push_back(llvm::Type::getDoubleTy(*target.context));
            } else {
                // ints
                std::set<size_t> sizes = { 1, 2, 3, 4, 8 };

                auto bound = sizes.lower_bound(bytesSoFar);
                auto intType = llvm::IntegerType::get(*target.context, *bound * 8);

                result.push_back(intType);
            }

            bytesSoFar = 0;
            allFloats = true;
            allDoubles = true;
        };

        for (auto type : types) {
            auto size = target.layout->getTypeStoreSize(type);

            // too big to fit in a register, somehow
            if (size > dwordSize)
                return std::nullopt;

            // need to align this
            if (bytesSoFar + size > dwordSize)
                push();

            bytesSoFar += size;

            if (!type->isFloatTy())
                allFloats = false;

            if (!type->isDoubleTy())
                allDoubles = false;
        }

        if (bytesSoFar > 0)
            push();

        if (result.size() > maxTypeCount)
            return std::nullopt;

        return result;
    }

    std::optional<std::vector<llvm::Type *>> getSysVLLVMTypes(
        const builder::Target &target, llvm::Type *root) {
        auto size = target.layout->getTypeStoreSize(root);

        // 4 ints, 2 double words
        constexpr size_t maxInPlaceSize = 8 * 2;

        if (size > maxInPlaceSize)
            return std::nullopt;

        auto types = flattenLLVMType(root);

        if (!types)
            return std::nullopt;

        return combineSysVLLVMTypes(target, *types);
    }

    FormatArgumentsResult SysVPlatform::formatArguments(
        const Target &target, const FormatArgumentsPackage &package) {
        FormatArgumentsResult result;

        // ignoring floats used/max floats registers for now
//        uint32_t floatsUsed = 0;

        // dropping attributes for now as well, seem like they aren't super important, maybe

        auto sysVReturnTypes = getSysVLLVMTypes(target, package.returnType);

        if (sysVReturnTypes) {
            if (sysVReturnTypes->size() == 1) {
                result.returnType = sysVReturnTypes->front();
            } else {
                result.returnType = llvm::StructType::get(*target.context, *sysVReturnTypes, false);
            }
        } else {
            auto pointerType = llvm::PointerType::get(package.returnType, 0);

            // mark sret

            result.returnType = llvm::Type::getVoidTy(*target.context);
            result.parameters.emplace_back(
                "returnVal", pointerType, llvm::AttrBuilder().addStructRetAttr(package.returnType));
        }

        for (const auto &[name, type] : package.parameters) {
            auto types = getSysVLLVMTypes(target, type);

            if (types) {
                if (types->size() == 1) {
                    result.parameters.emplace_back(
                        name, types->front(), llvm::AttrBuilder());
                } else {
                    for (size_t a = 0; a < types->size(); a++) {
                        result.parameters.emplace_back(
                            fmt::format("{}_{}", name, a), (*types)[a], llvm::AttrBuilder());
                    }
                }
            } else {
                auto pointerType = llvm::PointerType::get(type, 0);

                // mark byval

                result.parameters.emplace_back(
                    name, pointerType, llvm::AttrBuilder().addByValAttr(type));
            }
        }

        return result;
    }

    llvm::Value *SysVPlatform::invokeFunction(
        const ops::Context &context,
        llvm::FunctionCallee function,
        llvm::Type *returnType,
        const std::vector<llvm::Value *> &values) {
        if (!context.ir)
            return nullptr;

        assert(context.function);

        std::vector<llvm::Value *> formattedValues;
        formattedValues.reserve(values.size()); // at least

        // first process return type
        // alloca
        llvm::Value *sretPointer = nullptr;
        auto sysVReturnTypes = getSysVLLVMTypes(context.builder.target, returnType);

        if (!sysVReturnTypes) {
            // we have to worry about sret

            sretPointer = context.function->entry.CreateAlloca(returnType);
            formattedValues.push_back(sretPointer); // lol
        }

        for (auto value : values) {
            auto baseType = value->getType();
            auto sysVTypes = getSysVLLVMTypes(context.builder.target, baseType);

            if (sysVTypes) {
                if (sysVTypes->size() == 1 && sysVTypes->front()->getTypeID() == baseType->getTypeID()) {
                    // no struct shenanigans
                    formattedValues.push_back(value);
                } else {
                    // struct shenanigans

                    // i have my own custom struct
                    // make llvm base struct from sysVTypes
                    // alloca a llvm base struct
                    // memcpy intrinsic over base struct
                    // load each parameter from struct,
                    // pass each parameter

                    auto sysVStruct = llvm::StructType::get(context.builder.context, *sysVTypes);
                    auto data = context.function->entry.CreateAlloca(sysVStruct);
                    auto valueData = context.function->entry.CreateAlloca(baseType);
                    context.ir->CreateStore(value, valueData);

                    auto i8PtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                    auto i8Data = context.ir->CreatePointerCast(data, i8PtrType);
                    auto i8ValueData = context.ir->CreatePointerCast(valueData, i8PtrType);

                    auto valueSize = context.builder.target.layout->getTypeStoreSize(baseType);
                    auto sysVSize = context.builder.target.layout->getTypeStoreSize(sysVStruct);

                    assert(sysVSize >= valueSize);

                    context.ir->CreateMemCpy(
                        i8Data, llvm::MaybeAlign(),
                        i8ValueData, llvm::MaybeAlign(),
                        valueSize);

                    // data holds something good now

                    for (size_t a = 0; a < sysVTypes->size(); a++) {
                        auto sysVSubType = (*sysVTypes)[a];

                        auto pointer = context.ir->CreateStructGEP(sysVStruct, data, a);
                        auto pointerValue = context.ir->CreateLoad(sysVSubType, pointer);

                        formattedValues.push_back(pointerValue);
                    }
                }
            } else {
                // classic byval

                // i have my own custom struct, too big
                // i need to pass by pointer
                // so i alloca unfortunately?
                // copy it in with a store possibly
                // pass pointer as arg

                auto data = context.function->entry.CreateAlloca(baseType);
                context.ir->CreateStore(value, data);

                formattedValues.push_back(data); // ?
            }
        }

        auto result = context.ir->CreateCall(function, formattedValues);

        if (sretPointer) {
            return context.ir->CreateLoad(returnType, sretPointer);
        } else {
            return result;
        }
    }

    std::vector<llvm::Value *> SysVPlatform::tieArguments(
        const ops::Context &context,
        llvm::Type *returnType,
        const std::vector<llvm::Type *> &argumentTypes,
        const std::vector<llvm::Argument *> &arguments) {
        // i have spread out arguments
        // should probably go through expected argument types, split them out
        // check if they're what i get in values
        // if they are, alloca a struct
        // load the values in that struct
        // alloca a base struct
        // memcpy old struct over new struct
        // return that instead

        assert(context.ir && context.function);

        std::vector<llvm::Value *> formattedValues;
        formattedValues.reserve(argumentTypes.size());

        auto returnSysVTypes = getSysVLLVMTypes(context.builder.target, returnType);

        size_t argumentIndex = 0;

        if (!returnSysVTypes) // sret
            argumentIndex++;

        auto pop = [&arguments, &argumentIndex]() {
            return arguments[argumentIndex++];
        };

        for (auto argumentType : argumentTypes) {
            auto sysVTypes = getSysVLLVMTypes(context.builder.target, argumentType);

            if (sysVTypes) {
                if (sysVTypes->size() == 1 && sysVTypes->front()->getTypeID() == argumentType->getTypeID()) {
                    // no struct shenanigans
                    formattedValues.push_back(pop());
                } else {
                    // struct shenanigans

                    auto sysVStruct = llvm::StructType::get(context.builder.context, *sysVTypes);
                    auto data = context.function->entry.CreateAlloca(sysVStruct);

                    for (size_t a = 0; a < sysVTypes->size(); a++) {
                        auto sysVSubType = (*sysVTypes)[a];
                        auto value = pop();

                        assert(sysVSubType->getTypeID() == value->getType()->getTypeID());

                        auto pointer = context.ir->CreateStructGEP(sysVStruct, data, a);
                        context.ir->CreateStore(value, pointer);
                    }

                    // struct is loaded now
                    auto valueData = context.function->entry.CreateAlloca(argumentType);

                    auto i8PtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                    auto i8Data = context.ir->CreatePointerCast(data, i8PtrType);
                    auto i8ValueData = context.ir->CreatePointerCast(valueData, i8PtrType);

                    auto valueSize = context.builder.target.layout->getTypeStoreSize(argumentType);
                    auto sysVSize = context.builder.target.layout->getTypeStoreSize(sysVStruct);

                    assert(sysVSize >= valueSize);

                    context.ir->CreateMemCpy(
                        i8ValueData, llvm::MaybeAlign(),
                        i8Data, llvm::MaybeAlign(),
                        valueSize);

                    formattedValues.push_back(context.ir->CreateLoad(argumentType, valueData));
                }
            } else {
                // handle byval
                formattedValues.push_back(context.ir->CreateLoad(argumentType, pop())); // assume pointer
            }
        }

        assert(argumentIndex == arguments.size());
        assert(formattedValues.size() == argumentTypes.size());

        return formattedValues;
    }

    void SysVPlatform::tieReturn(
        const ops::Context &context,
        llvm::Type *returnType,
        llvm::Value *value,
        const std::vector<llvm::Argument *> &arguments) {
        assert(context.ir && context.function);

        if (!value) {
            context.ir->CreateRetVoid();
            return;
        }

        auto sysVTypes = getSysVLLVMTypes(context.builder.target, returnType);

        if (sysVTypes) {
            if (sysVTypes->size() == 1 && sysVTypes->front()->getTypeID() == returnType->getTypeID()) {
                // no struct shenanigans
                context.ir->CreateRet(value);
            } else {
                // struct shenanigans

                auto sysVStruct = llvm::StructType::get(context.builder.context, *sysVTypes);
                auto data = context.function->entry.CreateAlloca(sysVStruct);
                auto valueData = context.function->entry.CreateAlloca(returnType);
                context.ir->CreateStore(value, valueData);

                auto i8PtrType = llvm::Type::getInt8PtrTy(context.builder.context);
                auto i8Data = context.ir->CreatePointerCast(data, i8PtrType);
                auto i8ValueData = context.ir->CreatePointerCast(valueData, i8PtrType);

                auto valueSize = context.builder.target.layout->getTypeStoreSize(returnType);
                auto sysVSize = context.builder.target.layout->getTypeStoreSize(sysVStruct);

                assert(sysVSize >= valueSize);

                context.ir->CreateMemCpy(
                    i8Data, llvm::MaybeAlign(),
                    i8ValueData, llvm::MaybeAlign(),
                    valueSize);

                // data holds something good now

                context.ir->CreateRet(context.ir->CreateLoad(sysVStruct, data));
            }
        } else {
            // sret

            // uhh, a bit indirect but
            assert(!arguments.empty());
            auto sretArgument = arguments.front();
            assert(sretArgument->getType()->isPointerTy());

            context.ir->CreateStore(value, sretArgument);
            context.ir->CreateRetVoid();
        }
    }
}