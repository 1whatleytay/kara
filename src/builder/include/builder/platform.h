#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <vector>
#include <memory>

namespace kara::builder {
    struct Target;

    namespace ops {
        struct Context;
    }

    struct FormatArgumentsPackage {
        llvm::Type *returnType;
        std::vector<std::pair<std::string, llvm::Type *>> parameters;

        [[nodiscard]] std::vector<llvm::Type *> parameterTypes() const;
    };

    struct Platform {
        // not very safe but works I hope
        virtual FormatArgumentsPackage formatArguments(
            const Target &target,
            const FormatArgumentsPackage &package);

        virtual llvm::Value *invokeFunction(
            const ops::Context &context,
            llvm::FunctionCallee function,
            llvm::Type *returnType,
            const std::vector<llvm::Value *> &values);

        virtual std::vector<llvm::Value *> tieArguments(
            const ops::Context &context,
            llvm::Type *returnType,
            const std::vector<llvm::Type *> &argumentTypes,
            const std::vector<llvm::Value *> &arguments);

        virtual void tieReturn(
            const ops::Context &context,
            llvm::Type *returnType,
            llvm::Value *value,
            const std::vector<llvm::Value *> &arguments);

        static std::unique_ptr<Platform> byNative();
        static std::unique_ptr<Platform> byTriple(const std::string &name);

        virtual ~Platform() = default;
    };

    struct SysVPlatform : public Platform {
        FormatArgumentsPackage formatArguments(
            const Target &target,
            const FormatArgumentsPackage &package) override;

        llvm::Value *invokeFunction(
            const ops::Context &context,
            llvm::FunctionCallee function,
            llvm::Type *returnType,
            const std::vector<llvm::Value *> &values) override;

        std::vector<llvm::Value *> tieArguments(
            const ops::Context &context,
            llvm::Type *returnType,
            const std::vector<llvm::Type *> &argumentTypes,
            const std::vector<llvm::Value *> &arguments) override;

        void tieReturn(
            const ops::Context &context,
            llvm::Type *returnType,
            llvm::Value *value,
            const std::vector<llvm::Value *> &arguments) override;
    };
}
