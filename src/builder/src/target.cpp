#include <builder/target.h>

#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>

#include <fmt/format.h>

namespace kara::builder {
    Target::Target(const std::string &suggestedTriple, bool allTargets) {
        triple = suggestedTriple.empty() ? llvm::sys::getDefaultTargetTriple() : suggestedTriple;

        if (triple.empty())
            throw std::runtime_error("Unknown default triple.");

        if (allTargets)
            llvm::InitializeAllTargets();
        else
            llvm::InitializeNativeTarget();

//        LLVMInitializeX86TargetInfo();
//
//        LLVMInitializeX86Target();
//        LLVMInitializeX86TargetMC();
//
//        LLVMInitializeX86AsmParser();
//        LLVMInitializeX86AsmPrinter();

        // Web Assembly...
        // ARM :D

        std::string error;
        target = llvm::TargetRegistry::lookupTarget(triple, error);

        if (!target)
            throw std::runtime_error(fmt::format("Cannot find target for triple {}.\n", triple));

        llvm::TargetOptions targetOptions;
        llvm::Optional<llvm::Reloc::Model> model;
        machine = target->createTargetMachine(triple, "generic", "", targetOptions, model);

        if (!machine)
            throw std::runtime_error(fmt::format("Cannot find machine for triple {}.\n", triple));

        layout = std::make_unique<llvm::DataLayout>(machine->createDataLayout());

        context = std::make_unique<llvm::LLVMContext>();
    }
}
