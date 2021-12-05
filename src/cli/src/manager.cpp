#include <cli/manager.h>

#include <cli/log.h>

#include <builder/error.h>
#include <builder/builder.h>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>

#include <lld/Common/Driver.h>

#include <fstream>
#include <sstream>

namespace kara::cli {
    void tracePackages(const TargetConfig &config, PackageManager &packages, ConfigHold &hold, ConfigMap &out) {
        for (const auto &package : config.packages) {
            if (out.find(package.first) != out.end())
                continue;

            assert(package.second.size() == 1); // okay...

            auto name = package.second.front();

            auto paths = packages.install(package.first, name);

            assert(paths.size() == 1);

            // concerning...
            for (const auto &path : paths) {
                auto loaded = TargetConfig::loadFromThrows(path);
                auto ptr = std::make_unique<TargetConfig>(std::move(loaded));
                auto ref = ptr.get();

                hold.push_back(std::move(ptr));

                out[name] = ref;
                tracePackages(*ref, packages, hold, out);
            }
        }

        for (const auto &imported : config.configs)
            tracePackages(imported, packages, hold, out);
    }

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

    // NOLINT(readability-convert-member-functions-to-static)
    fs::path ProjectManager::createTargetDirectory(const std::string &target) {
        fs::path directory = fs::path(main.outputDirectory) / target;

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

        auto targetIt = configs.find(target);
        if (targetIt == configs.end())
            throw std::runtime_error(fmt::format("Cannot find target {} in project file.", target));

        auto &targetConfig = targetIt->second;

        auto result = std::make_unique<TargetResult>();

        result->depends = std::vector<std::string>(targetConfig->configs.size());
        std::transform(targetConfig->configs.begin(), targetConfig->configs.end(), result->depends.begin(),
            [](const auto &r) { return r.resolveName(); });

        result->linkerOptions = targetConfig->linkerOptions;

        result->libraries = targetConfig->libraries;
        result->dynamicLibraries = targetConfig->dynamicLibraries;

        if (!targetConfig->includes.empty()) {
            std::vector<fs::path> paths;
            paths.reserve(targetConfig->includes.size());
            std::transform(targetConfig->includes.begin(), targetConfig->includes.end(), std::back_inserter(paths),
                [](const auto &r) { return fs::path(r); });

            result->includes.push_back(builder::Library {
                std::move(paths),
                targetConfig->includeArguments,
            });
        }

        // Add packages to list of targets to build.
        for (const auto &package : targetConfig->packages) {
            assert(package.second.size() == 1); // mistake

            result->depends.push_back(package.second.front());
        }

        // Build targets that are depended on
        for (const auto &toBuild : result->depends) {
            // is a cycle possible here?
            auto &otherResult = makeTarget(toBuild, root, linkerType);

            result->defaultOptions.merge(otherResult.defaultOptions);

            result->libraries.insert(result->libraries.end(),
                otherResult.libraries.begin(), otherResult.libraries.end());

            result->dynamicLibraries.insert(result->dynamicLibraries.end(),
                otherResult.dynamicLibraries.begin(), otherResult.dynamicLibraries.end());

            // might have duplicates
            result->includes.insert(result->includes.end(),
                otherResult.includes.begin(), otherResult.includes.end());

            result->linkerOptions.insert(
                result->linkerOptions.begin(), otherResult.linkerOptions.begin(), otherResult.linkerOptions.end());
        }

        result->defaultOptions.merge(targetConfig->defaultOptions);

        if (targetConfig->type == TargetType::Interface) {
            auto &ref = *result;
            updatedTargets.insert({ target, std::move(result) });

            return ref;
        }

        log(LogSource::targetStart, "Building target {}", target);

        auto &options = result->defaultOptions;

        auto directory = createTargetDirectory(target);
        auto outputFile = directory / fmt::format("{}.o", target);

        log(LogSource::target, "Building {}", outputFile.string());

        builder::SourceManager manager(database, result->includes);

        std::vector<std::unique_ptr<llvm::Module>> modules;
        for (const auto &file : targetConfig->files) {
            auto &managerFile = manager.get(file);

            try {
                kara::builder::Builder builder { managerFile, manager, builderTarget, options };

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

        auto base = std::make_unique<llvm::Module>(target, *builderTarget.context);
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

            auto emitResult = builderTarget.machine->addPassesToEmitFile(
                passManager, output, nullptr, llvm::CodeGenFileType::CGFT_ObjectFile);

            if (emitResult)
                throw std::runtime_error("Target machine does not support object output.");

            passManager.run(*result->module);
        }

        if (targetConfig->type == TargetType::Executable) {
            auto linkFile = directory / target;

            logHeader(LogSource::target);
            fmt::print("Linking ");
            fmt::print(fmt::emphasis::italic, "{}\n", linkFile.string());

            std::unordered_set<std::string> libraryPaths;
            std::unordered_set<std::string> libraryNames;

            for (const auto &libraryPath : result->libraries) {
                fs::path path(libraryPath);

                auto file = path.filename().string();

                std::string prefix = "lib";
                std::string postfix = ".a";

                if (file.size() > prefix.size() && file.size() > postfix.size()
                    && file.substr(0, prefix.size()) == prefix
                    && file.substr(file.size() - postfix.size(), postfix.size()) == postfix) {
                    file = file.substr(prefix.size(), file.size() - prefix.size() - postfix.size());
                }

                libraryPaths.insert(fs::absolute(path.parent_path()).string());
                libraryNames.insert(file);
            }

            std::vector<std::string> arguments = {
                root,
                outputFile.string(),
                "-o",
                linkFile.string(),
                "-arch",
                "x86_64", // yikes
                "-lSystem", // macos only...
                "-L/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib",
                "-syslibroot",
                "/Library/Developer/CommandLineTools/SDKs/MacOSX11.sdk",
                "-platform_version",
                "macos",
                "12.0.0",
                "12.0.0",
            };

            for (const auto &path : libraryPaths)
                arguments.push_back(fmt::format("-L{}", path));

            for (const auto &name : libraryNames)
                arguments.push_back(fmt::format("-l{}", name));

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

    ProjectManager::ProjectManager(const TargetConfig &main, const std::string &triple, const std::string &root)
        : main(main) // cannot std::move because i need the data later in constructor
        , builderTarget(triple)
        , database(managerCallback)
        , packages(main.packagesDirectory, root) {
        configs = this->main.resolveConfigs(); // oya

        tracePackages(this->main, packages, packageConfigs, configs);
    }
}
