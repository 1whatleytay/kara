#include <builder/platform.h>

#include <builder/target.h>
#include <builder/operations.h>

#include <llvm/ADT/Triple.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>

namespace kara::builder {
    std::vector<llvm::Type *> FormatArgumentsPackage::parameterTypes() const {
        std::vector<llvm::Type *> result;
        result.reserve(parameters.size());

        for (const auto &p : parameters)
            result.push_back(p.second);

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

    FormatArgumentsPackage Platform::formatArguments(
        const Target &target,
        const FormatArgumentsPackage &package) {
        return package;
    }

    llvm::Value *Platform::invokeFunction(
        const ops::Context &context,
        llvm::FunctionCallee function,
        const std::vector<llvm::Value *> &values) {
        if (context.ir) {
            return context.ir->CreateCall(function, values);
        }

        return nullptr;
    }

    std::vector<llvm::Value *> Platform::tieArguments(
        const ops::Context &context,
        const std::vector<llvm::Type *> &argumentTypes,
        const std::vector<llvm::Value *> &arguments) {
        return arguments;
    }

    void Platform::tieReturn(
        const ops::Context &context,
        llvm::Type *returnType,
        llvm::Value *value) {
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

    FormatArgumentsPackage SysVPlatform::formatArguments(
        const Target &target, const FormatArgumentsPackage &package) {
        FormatArgumentsPackage result;

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
            result.parameters.emplace_back("returnVal", pointerType);
        }

        for (const auto &param : package.parameters) {
            auto types = getSysVLLVMTypes(target, param.second);

            if (types) {
                if (types->size() == 1) {
                    result.parameters.emplace_back(param.first, types->front());
                } else {
                    for (size_t a = 0; a < types->size(); a++) {
                        result.parameters.emplace_back(fmt::format("{}_{}", param.first, a), (*types)[a]);
                    }
                }
            } else {
                auto pointerType = llvm::PointerType::get(param.second, 0);

                // mark byval

                result.parameters.emplace_back(param.first, pointerType);
            }
        }

        return result;
    }

    llvm::Value *SysVPlatform::invokeFunction(
        const ops::Context &context,
        llvm::FunctionCallee function,
        const std::vector<llvm::Value *> &values) {
        // pita
        throw;
    }

    std::vector<llvm::Value *> SysVPlatform::tieArguments(
        const ops::Context &context,
        const std::vector<llvm::Type *> &argumentTypes,
        const std::vector<llvm::Value *> &arguments) {
        throw;
    }

    void SysVPlatform::tieReturn(
        const ops::Context &context,
        llvm::Type *returnType,
        llvm::Value *value) {
        throw;
    }
}