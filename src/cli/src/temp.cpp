if (options.optimize) {
    llvm::FunctionAnalysisManager fam;

    fam.registerPass([]() { return llvm::PassInstrumentationAnalysis(); });
    fam.registerPass([]() { return llvm::DominatorTreeAnalysis(); });
    fam.registerPass([]() { return llvm::AssumptionAnalysis(); });
    fam.registerPass([]() { return llvm::LazyValueAnalysis(); });
    fam.registerPass([]() { return llvm::TargetLibraryAnalysis(); });
    fam.registerPass([]() { return llvm::TargetIRAnalysis(); });

    llvm::ModuleAnalysisManager mam;
    mam.registerPass([]() { return llvm::PassInstrumentationAnalysis(); });

    mam.registerPass([&fam]() { return llvm::FunctionAnalysisManagerModuleProxy(fam); });
    fam.registerPass([&mam]() { return llvm::ModuleAnalysisManagerFunctionProxy(mam); });

    llvm::FunctionPassManager fpm;
    fpm.addPass(llvm::LowerSwitchPass());
    fpm.addPass(llvm::SimplifyCFGPass());
    fpm.addPass(llvm::PromotePass());

    llvm::ModulePassManager mpm;
    mpm.addPass(createModuleToFunctionPassAdaptor(std::move(fpm)));
    mpm.run(*base, mam);
}

if (options.printIR)
    base->print(llvm::outs(), nullptr);

if (!options.output.empty()) {
    llvm::legacy::PassManager pass;

    std::error_code error;
    llvm::raw_fd_ostream output(options.output, error);

    if (error)
        throw std::runtime_error(fmt::format("Cannot open file {} for output", options.output));

    if (target.machine->addPassesToEmitFile(pass, output, nullptr, llvm::CodeGenFileType::CGFT_ObjectFile))
        throw std::runtime_error("Target machine does not support object output.");

    pass.run(*base);
}

if (options.interpret) {
    auto expectedJit = llvm::orc::LLJITBuilder().create();

    if (!expectedJit)
        throw std::runtime_error("Could not create jit instance.");

    auto &jit = expectedJit.get();

    llvm::orc::ThreadSafeModule tsModule(std::move(base), std::move(context));

    if (jit->addIRModule(std::move(tsModule)))
        throw std::runtime_error("Could not add module to jit instance.");

    for (const auto &library : libraries) {
        for (const auto &lib : library.libraries) {
            auto loader = llvm::orc::StaticLibraryDefinitionGenerator::Load(jit->getObjLinkingLayer(), lib.c_str());

            if (!loader) {
                fmt::print("Failed to load library {}.\n", lib.string());
            } else {
                jit->getMainJITDylib().addGenerator(std::move(loader.get()));
            }
        }

        for (const auto &lib : library.dynamicLibraries) {
            auto loader = llvm::orc::DynamicLibrarySearchGenerator::Load(lib.c_str(), target.layout->getGlobalPrefix());

            if (!loader) {
                fmt::print("Failed to load dynamic library {}.\n", lib.string());
            } else {
                jit->getMainJITDylib().addGenerator(std::move(loader.get()));
            }
        }
    }

    auto expectedMain = jit->lookup("main");
    if (!expectedMain)
        throw std::runtime_error("Could not find main symbol.");

    auto *entry = reinterpret_cast<int (*)()>(expectedMain.get().getAddress());

    fmt::print("Returned {}.\n", entry());
}

struct TransferState {
    enum class Stage {
        NoUpdate,
        ReceivingObjects,
        ResolvingDeltas,
        Done,
    };

    Stage stage = Stage::NoUpdate;
    int pin = 0;

    bool movePin(uint32_t value, uint32_t max) {
        int newPin = value / max * 10;
        while (newPin > pin) {
            fmt::print(".");
            pin++;
        }

        constexpr auto threshold = 10;

        return pin >= threshold;
    }
} state;

auto transfer = [](const git_indexer_progress *stats, void *payload) -> int {
    // received objects

    auto state = reinterpret_cast<TransferState *>(payload);

    switch (state->stage) {
    case TransferState::Stage::NoUpdate:
        fmt::print("\nReceiving Objects");
        state->stage = TransferState::Stage::ReceivingObjects;
        state->pin = 0;

        break;

    case TransferState::Stage::ReceivingObjects:
        if (state->movePin(stats->received_objects, stats->total_objects)) {
            fmt::print("\nResolving Deltas");
            state->stage = TransferState::Stage::ResolvingDeltas;
            state->pin = 0;
        }

        break;

    case TransferState::Stage::ResolvingDeltas:
        if (state->movePin(stats->indexed_deltas, stats->total_deltas)) {
            fmt::print("\n{} Bytes Transferred.\n", stats->received_bytes);
            state->stage = TransferState::Stage::Done;
            state->pin = 0;
        }

        break;

    case TransferState::Stage::Done:
        break;

    default:
        throw;
    }

    return 0;
};
