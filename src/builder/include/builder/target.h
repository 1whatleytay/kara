#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>

#include <string>

namespace kara::builder {
    struct Target {
        std::string triple;
        const llvm::Target *target;
        llvm::TargetMachine *machine;
        std::unique_ptr<llvm::DataLayout> layout;

        std::unique_ptr<llvm::LLVMContext> context;

        explicit Target(const std::string &suggestedTriple, bool allTargets = false);
    };
}
