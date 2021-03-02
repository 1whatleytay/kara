#include <builder/builder.h>

#include <parser/type.h>
#include <parser/function.h>

#include <llvm/Support/Host.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>

#include <fmt/printf.h>

#include <filesystem>

namespace fs = std::filesystem;

BuilderResult::BuilderResult(Kind kind, Value *value, Typename type, std::unique_ptr<BuilderResult> implicit)
    : kind(kind), value(value), type(std::move(type)), implicit(std::move(implicit)) { }

BuilderType *Builder::makeType(const TypeNode *node) {
    auto iterator = types.find(node);

    if (iterator == types.end()) {
        auto ptr = std::make_unique<BuilderType>(node, *this);

        BuilderType *result = ptr.get();
        types[node] = std::move(ptr);

        result->build(); // needed to avoid recursive problems

        return result;
    } else {
        return iterator->second.get();
    }
}

BuilderFunction *Builder::makeFunction(const FunctionNode *node) {
    auto iterator = functions.find(node);

    if (iterator == functions.end()) {
        auto ptr = std::make_unique<BuilderFunction>(node, *this);

        BuilderFunction *result = ptr.get();
        functions[node] = std::move(ptr);

        result->build();

        return result;
    } else {
        return iterator->second.get();
    }
}

bool BuilderTarget::valid() const {
    return !triple.empty() && target && machine && layout;
}

BuilderTarget::BuilderTarget(const std::string &suggestedTriple) {
    triple = suggestedTriple.empty() ? sys::getDefaultTargetTriple() : suggestedTriple;

    LLVMInitializeX86TargetInfo();

    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();

    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    std::string error;
    target = TargetRegistry::lookupTarget(triple, error);

    TargetOptions targetOptions;
    Optional<Reloc::Model> model;
    machine = target->createTargetMachine(triple, "generic", "", targetOptions, model);

    layout = std::make_unique<DataLayout>(machine->createDataLayout());
}

Builder::Builder(RootNode *root, Options opts) : root(root), options(std::move(opts)), target(options.triple) {
    context = std::make_unique<LLVMContext>();
    module = std::make_unique<Module>(fs::path(options.inputFile).filename().string(), *context);

    if (!target.valid())
        throw std::runtime_error("Could not initialize target.");

    module->setDataLayout(*target.layout);
    module->setTargetTriple(target.triple);

    for (const auto &node : root->children) {
        switch (node->is<Kind>()) {
            case Kind::Type:
                makeType(node->as<TypeNode>());
                break;

            case Kind::Function:
                makeFunction(node->as<FunctionNode>());
                break;

            default:
                assert(false);
        }
    }

    if (options.printIR)
        module->print(llvm::outs(), nullptr);

    if (verifyModule(*module, &llvm::errs(), nullptr))
        throw std::runtime_error("Aborted.");

    if (!options.outputFile.empty()) {
        legacy::PassManager manager;

        std::error_code error;
        raw_fd_ostream output(options.outputFile, error);

        if (error)
            throw std::runtime_error(fmt::format("Cannot open file {} for output", options.outputFile));

        if (target.machine->addPassesToEmitFile(manager, output, nullptr, CodeGenFileType::CGFT_ObjectFile))
            throw std::runtime_error("Target machine does not support object output.");

        manager.run(*module);
    }

    if (options.interpret) {
        auto expectedJit = orc::LLJITBuilder().create();

        if (!expectedJit)
            throw std::runtime_error("Could not create jit instance.");

        auto &jit = expectedJit.get();

        if (jit->addIRModule(orc::ThreadSafeModule(std::move(module), std::move(context))))
            throw std::runtime_error("Could not add module to jit instance.");

        auto expectedMain = jit->lookup("main");
        if (!expectedMain)
            throw std::runtime_error("Could not find main symbol.");

        auto *main = reinterpret_cast<int(*)()>(expectedMain.get().getAddress());

        fmt::print("Returned {}.\n", main());
    }
}
