#include <cli/manager.h>

#include <cli/log.h>

#include <builder/error.h>
#include <builder/builder.h>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>

#include <lld/Common/Driver.h>

namespace kara::cli {
    std::string invokeLinker(const std::string &linker, const std::vector<std::string> &arguments) {
        using LinkFunction = bool (*)(
            llvm::ArrayRef<const char *>, bool,
            llvm::raw_ostream &, llvm::raw_ostream &);

        std::vector<const char *> cstrings(arguments.size());
        std::transform(arguments.begin(), arguments.end(), cstrings.begin(), [](const auto &e) {
            return e.c_str();
        });

        std::unordered_map<std::string, LinkFunction> functions = {
            { "elf", lld::elf::link },
            { "macho", lld::macho::link },
            { "macho-old", lld::mach_o::link },
            { "wasm", lld::wasm::link },
            { "coff", lld::coff::link },
            { "mingw", lld::mingw::link },
        };

        auto function = functions.find(linker);
        if (function == functions.end())
            return fmt::format("Invalid linker name {}.", linker);

        auto value = function->second(cstrings, false, llvm::outs(), llvm::errs());
        if (!value)
            return "Linker failed.";

        return "";
    }

    fs::path ProjectManager::createTargetDirectory(
        const std::string &target) const { // NOLINT(modernize-use-nodiscard)
        fs::path directory = fs::path(config.outputDirectory) / target;

        if (!fs::is_directory(directory))
            fs::create_directories(directory);

        return directory;
    }

    void managerCallback(const fs::path &path, const std::string &type) {
        LogSource source = LogSource::target;

        if (type.empty() || type == "kara") {
            source = LogSource::compileKara;
        } else if (type == "c") {
            source = LogSource::compileC;
        }

        logHeader(source);

        fmt::print("Preparing file ");
        fmt::print(fmt::emphasis::italic, "{}\n", path.string());
    }

    const TargetResult &ProjectManager::makeTarget(
        const std::string &target, const std::string &root, const std::string &linkerType) {
        auto it = updatedTargets.find(target);
        if (it != updatedTargets.end())
            return *it->second;

        auto targetIt = config.targets.find(target);
        if (targetIt == config.targets.end())
            throw std::runtime_error(fmt::format("Cannot find target {} in project file.", target));

        auto &targetConfig = targetIt->second;

        auto result = std::make_unique<TargetResult>();
        result->external = targetConfig.external;
        result->linkerOptions = targetConfig.linkerOptions;
        result->libraries = targetConfig.libraries;

        for (const auto &library : targetConfig.libraries) {
            auto &otherResult = makeTarget(library, root, linkerType);

            result->defaultOptions.merge(otherResult.defaultOptions);

            result->libraries.insert(otherResult.libraries.begin(), otherResult.libraries.end());
            result->external.insert(otherResult.external.begin(), otherResult.external.end());
            result->linkerOptions.insert(
                result->linkerOptions.begin(), otherResult.linkerOptions.begin(), otherResult.linkerOptions.end());
        }

        result->defaultOptions.merge(targetConfig.defaultOptions);

        if (targetConfig.type == TargetType::Interface) {
            auto &ref = *result;
            updatedTargets.insert({ target, std::move(result) });

            return ref;
        }

        log(LogSource::targetStart, "Building target {}", target);

        auto &options = result->defaultOptions;

        auto directory = createTargetDirectory(target);
        auto outputFile = directory / fmt::format("{}.o", target);

        log(LogSource::target, "Building {}", outputFile.string());

        // this isn't really caring for order, its just adding whatever no matter the target
        // not really desired but I don't have the brain power to think of a better system
        for (const auto &library : result->external)
            manager.add(fs::path(library));

        std::vector<std::unique_ptr<llvm::Module>> modules;
        for (const auto &file : targetConfig.files) {
            auto &managerFile = manager.get(file);

            try {
                kara::builder::Builder builder { managerFile, options };

                LogSource source = LogSource::target;

                if (managerFile.type.empty() || managerFile.type == "kara") {
                    source = LogSource::compileKara;
                } else if (managerFile.type == "c") {
                    source = LogSource::compileC;
                }

                logHeader(source);

                fmt::print("Building file ");
                fmt::print(fmt::emphasis::italic, "{}\n", managerFile.path.string());

                modules.push_back(std::move(builder.module));
            } catch (const kara::builder::VerifyError &error) {
                hermes::LineDetails details(managerFile.state->text, error.node->index, false);

                fmt::print("{} [line {}]\n{}\n{}\n", error.issue, details.lineNumber, details.line, details.marker);

                throw;
            }
        }

        auto base = std::make_unique<llvm::Module>(target, *manager.context);
        llvm::Linker linker(*base);

        for (auto &module : modules)
            linker.linkInModule(std::move(module));

        modules.clear();

        result->module = std::move(base);

        if (llvm::verifyModule(*result->module, &llvm::errs()))
            throw std::runtime_error(fmt::format("Module for target {} failed to verify.", target));


        logHeader(LogSource::target);
        fmt::print("Writing ");
        fmt::print(fmt::emphasis::italic, "{}\n", outputFile.string());

        {
            llvm::legacy::PassManager passManager;

            std::error_code error;
            llvm::raw_fd_ostream output(outputFile.string(), error);

            if (error)
                throw std::runtime_error(fmt::format("Cannot open file {} for output", outputFile.string()));

            auto emitResult = manager.target.machine->addPassesToEmitFile(
                passManager, output, nullptr, llvm::CodeGenFileType::CGFT_ObjectFile);

            if (emitResult)
                throw std::runtime_error("Target machine does not support object output.");

            passManager.run(*result->module);
        }

        if (targetConfig.type == TargetType::Executable) {
            auto linkFile = directory / target;


            logHeader(LogSource::target);
            fmt::print("Linking ");
            fmt::print(fmt::emphasis::italic, "{}\n", linkFile.string());

            std::vector<std::string> arguments = {
                root,
                outputFile.string(),
                "-o",
                linkFile.string(),
                "-arch",
                "x86_64",
                "-lSystem",
                "-L/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib",
                "-syslibroot",
                "/Library/Developer/CommandLineTools/SDKs/MacOSX11.sdk",
                "-platform_version",
                "macos",
                "11.0.0",
                "11.0.0",
            };

            auto &linkOpts = result->linkerOptions;

            arguments.insert(arguments.end(), linkOpts.begin(), linkOpts.end());

            auto linkerResult = invokeLinker(linkerType, arguments);
            if (!linkerResult.empty())
                throw std::runtime_error(linkerResult);
        }

        log(LogSource::targetDone, "Built target {}", target);

        auto &ref = *result;
        updatedTargets.insert({ target, std::move(result) });

        return ref;
    }

    ProjectManager::ProjectManager(ProjectConfig config, const std::string &triple)
        : config(std::move(config))
        , manager(triple, managerCallback) { }

}
