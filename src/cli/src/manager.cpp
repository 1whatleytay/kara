#include <cli/manager.h>

#include <cli/log.h>

#include <builder/error.h>
#include <builder/builder.h>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>

#include <lld/Common/Driver.h>

#include <yaml-cpp/yaml.h>

#include <cassert>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::cli {
    // could just be root as first param
    // duplicate code in TargetCache::add
    const TargetConfig *TargetCache::resolveImport(const TargetConfig &parent, const TargetImport &package) const {
        switch (package.detectedKind()) {
        case TargetImportKind::ProjectFile: {
            auto pathToConfig = fs::path(parent.root).parent_path() / package.path; // absolute

            auto it = configsByPath.find(pathToConfig.string());
            if (it == configsByPath.end()) {
                throw std::runtime_error(
                    fmt::format("Failed to resolve import in {} for {}.", parent.root, package.path));
            }

            return it->second;
        }

        case TargetImportKind::RepositoryUrl: {
            //            assert(package.targets.size() == 1); // for name...
            //            auto suggestedTarget = package.targets.front();
            //
            auto it = configsByUrl.find(package.path);

            if (it == configsByUrl.end()) {
                throw std::runtime_error(
                    fmt::format("Failed to resolve import in {} for {}.", parent.root, package.path));
            }

            return it->second;
        }

        case TargetImportKind::CMakePackage: {
            auto it = configsByCMake.find(package.path);

            if (it == configsByCMake.end()) {
                throw std::runtime_error(
                    fmt::format("Failed to resolve import in {} for {}.", parent.root, package.path));
            }

            return it->second;
        }

        default:
            throw;
        }
    }

    void TargetCache::add(const TargetConfig &config, PackageManager &packages) {
        {
            auto name = config.resolveName();

            auto it = configsByName.find(name);

            if (it != configsByName.end()) {
                throw std::runtime_error(
                    fmt::format("Name conflict between config files, name `{}` is taken by {} and {}.", name,
                        it->second->root, config.root));
            }

            configsByName[name] = &config;
        }

        for (const auto &package : config.import) {
            // watch for continue

            std::vector<std::string> paths;

            // set to a value if the following statements determine package.from is any of these types
            std::optional<std::string> urlValue;
            std::optional<std::string> pathValue;
            std::optional<std::string> cmakeValue;

            // I'm not concerned about checking if configsByName is filled to prevent infinity recursion
            // It's more of an information kind of deal that can be used by other functions

            // This can probably be taken out into another function
            switch (package.detectedKind()) {
            case TargetImportKind::ProjectFile: {
                // path

                auto pathToConfig = fs::path(config.root).parent_path() / package.path; // absolute

                if (configsByPath.find(pathToConfig.string()) != configsByPath.end())
                    continue;

                pathValue = pathToConfig.string();

                if (!fs::exists(pathToConfig)) {
                    throw std::runtime_error(fmt::format(
                        "Attempted to import file at {}, but file did not exist.", fs::absolute(pathToConfig)));
                }

                if (pathToConfig.filename() == "CMakeLists.txt") {
                    assert(package.targets.size() == 1); // for name...
                    auto suggestedTarget = package.targets.front();

                    auto result
                        = packages.build(pathToConfig, suggestedTarget, suggestedTarget, package.buildArguments);

                    paths = result.configFiles; // name?
                } else {
                    paths = { pathToConfig };
                }

                break;
            }

            case TargetImportKind::RepositoryUrl: {
                // needs cmake eh?
                std::string suggestedTarget;

                if (!package.targets.empty()) {
                    assert(package.targets.size() == 1); // for name...
                    suggestedTarget = package.targets.front();
                }

                if (configsByUrl.find(package.path) != configsByUrl.end())
                    continue;

                urlValue = package.path;
                paths = packages.install(package.path, suggestedTarget, package.buildArguments);

                break;
            }

            case TargetImportKind::CMakePackage: {
                paths = packages.buildCMakePackage(package.path).configFiles;

                cmakeValue = package.path;

                break;
            }
            }

            assert(paths.size() == 1);

            // fix later...
            auto resolvedPath = paths.front();

            auto loaded = TargetConfig::loadFromThrows(resolvedPath);
            auto ptr = std::make_unique<TargetConfig>(std::move(loaded)); // yikes move
            auto ref = ptr.get();

            configHold.push_back(std::move(ptr));

            ref->options.merge(package.options);

            // I think there's a lot of simplifications that can be done here
            if (urlValue)
                configsByUrl[*urlValue] = ref;
            if (pathValue)
                configsByPath[*pathValue] = ref;
            if (cmakeValue)
                configsByCMake[*cmakeValue] = ref;

            add(*ref, packages);
        }
    }

    std::string invokeLinker(const std::string &linker, const std::vector<std::string> &arguments) {
        using LinkFunction = bool (*)(llvm::ArrayRef<const char *>, bool, llvm::raw_ostream &, llvm::raw_ostream &);

        std::vector<const char *> cstrings(arguments.size());
        std::transform(arguments.begin(), arguments.end(), cstrings.begin(), [](const auto &e) { return e.c_str(); });

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

    std::string ProjectManager::createTargetDirectory(const std::string &target) {
        fs::path directory = fs::path(mainTarget.outputDirectory) / target;

        if (!fs::is_directory(directory))
            fs::create_directories(directory);

        return directory.string();
    }

    void managerCallback(const fs::path &path, const std::string &type) {
        LogSource source = LogSource::target;

        if (type.empty() || type == "kara") {
            source = LogSource::compileKara;
        } else if (type == "c") {
            source = LogSource::compileC;
        }

        if (logHeader(source)) {
            fmt::print("Preparing file ");
            fmt::print(fmt::emphasis::italic, "{}\n", path.string());
        }
    }

    const TargetConfig *ProjectManager::getTarget(const std::string &name) {
        auto targetIt = targetCache.configsByName.find(name);
        if (targetIt == targetCache.configsByName.end())
            throw std::runtime_error(fmt::format("Cannot find target {} in project file.", name));

        return targetIt->second;
    }

    const TargetInfo &ProjectManager::readTarget(const TargetConfig *target) {
        auto it = targetInfos.find(target);
        if (it != targetInfos.end())
            return *it->second;

        //        auto targetIt = targetCache.configsByName.find(target);
        //        if (targetIt == targetCache.configsByName.end())
        //            throw std::runtime_error(fmt::format("Cannot find target {} in project file.", target));

        //        auto &targetConfig = targetIt->second;
        auto targetConfig = target; // change later, is just test alias right now for target

        auto result = std::make_unique<TargetInfo>();

        result->depends = std::vector<const TargetConfig *>(targetConfig->import.size());
        std::transform(targetConfig->import.begin(), targetConfig->import.end(), result->depends.begin(),
            [this, targetConfig](const auto &import) { return targetCache.resolveImport(*targetConfig, import); });

        result->linkerOptions = targetConfig->options.linkerOptions;

        result->libraries = targetConfig->options.libraries;
        result->dynamicLibraries = targetConfig->options.dynamicLibraries;

        if (!targetConfig->options.includes.empty()) {
//            std::vector<std::string> paths;
//            paths.reserve(targetConfig->options.includes.size());
//            std::transform(targetConfig->options.includes.begin(), targetConfig->options.includes.end(),
//                std::back_inserter(paths), [](const auto &r) { return fs::path(r); });

            result->includes.push_back(builder::Library {
//                std::move(paths),
                targetConfig->options.includes,
                targetConfig->options.includeArguments,
            });
        }

        // Add packages to list of targets to build.
        //        for (const auto &package : targetConfig->packages) {
        //            assert(package.second.size() == 1); // mistake
        //
        //            result->depends.push_back(package.second.front());
        //        }

        // Build targets that are depended on
        for (const auto &toBuild : result->depends) {
            // is a cycle possible here?
            auto &otherResult = readTarget(toBuild);

            result->defaultOptions.merge(otherResult.defaultOptions);

            result->libraries.insert(
                result->libraries.end(), otherResult.libraries.begin(), otherResult.libraries.end());

            result->dynamicLibraries.insert(result->dynamicLibraries.end(), otherResult.dynamicLibraries.begin(),
                otherResult.dynamicLibraries.end());

            // might have duplicates
            result->includes.insert(result->includes.end(), otherResult.includes.begin(), otherResult.includes.end());

            result->linkerOptions.insert(
                result->linkerOptions.begin(), otherResult.linkerOptions.begin(), otherResult.linkerOptions.end());
        }

        result->defaultOptions.merge(targetConfig->options.defaultOptions);

        auto &ptr = *result;
        targetInfos[target] = std::move(result);

        return ptr;
    }

    const TargetResult &ProjectManager::makeTarget(
        const TargetConfig *target, const std::string &root, const std::string &linkerType) {
        auto it = updatedTargets.find(target);
        if (it != updatedTargets.end())
            return *it->second;

        //        auto targetIt = targetCache.configsByName.find(target);
        //        if (targetIt == targetCache.configsByName.end())
        //            throw std::runtime_error(fmt::format("Cannot find target {} in project file.", target));
        //
        //        auto &targetConfig = targetIt->second;

        auto targetConfig = target;
        auto name = target->resolveName();

        auto &targetInfo = readTarget(target);

        auto result = std::make_unique<TargetResult>(TargetResult { targetInfo, nullptr });

        for (const auto &toBuild : targetInfo.depends)
            makeTarget(toBuild, root, linkerType);

        if (targetConfig->type == TargetType::Interface) {
            auto &ref = *result;
            updatedTargets.insert({ target, std::move(result) });

            return ref;
        }

        log(LogSource::targetStart, "Building target {}", name);

        auto &options = targetInfo.defaultOptions;

        auto directory = createTargetDirectory(name);
        auto outputFile = fs::path(directory) / fmt::format("{}.o", name);

        log(LogSource::target, "Building {}", outputFile.string());

        builder::SourceManager manager(sourceDatabase, targetInfo.includes);

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

                if (logHeader(source)) {
                    fmt::print("Building file ");
                    fmt::print(fmt::emphasis::italic, "{}\n", managerFile.path);
                }

                modules.push_back(std::move(builder.module));
            } catch (const kara::builder::VerifyError &error) {
                hermes::LineDetails details(managerFile.state->text, error.node->index, false);

                fmt::print("{} [line {}]\n{}\n{}\n", error.issue, details.lineNumber, details.line, details.marker);

                throw;
            }
        }

        auto base = std::make_unique<llvm::Module>(name, *builderTarget.context);
        llvm::Linker linker(*base);

        for (auto &module : modules)
            linker.linkInModule(std::move(module));

        modules.clear();

        result->module = std::move(base);

        if (llvm::verifyModule(*result->module, &llvm::errs())) {
            fmt::print("Module IR:\n");
            result->module->print(llvm::outs(), nullptr);
            fmt::print("\n");

            throw std::runtime_error(fmt::format("Module for target {} failed to verify.", name));
        }

        if (logHeader(LogSource::target)) {
            fmt::print("Writing ");
            fmt::print(fmt::emphasis::italic, "{}\n", outputFile.string());
        }

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
            auto linkFile = fs::path(directory) / name;

            if (logHeader(LogSource::target)) {
                fmt::print("Linking ");
                fmt::print(fmt::emphasis::italic, "{}\n", linkFile.string());
            }

            std::unordered_set<std::string> libraryPaths;
            std::unordered_set<std::string> libraryNames;

            for (const auto &libraryPath : targetInfo.libraries) {
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

            std::vector<std::string> arguments = { root, outputFile.string(), "-o", linkFile.string() };

            auto additionalArguments = platform->defaultLinkerArguments();
            arguments.insert(arguments.end(), additionalArguments.begin(), additionalArguments.end());

            for (const auto &path : libraryPaths)
                arguments.push_back(fmt::format("-L{}", path));

            for (const auto &lib : libraryNames)
                arguments.push_back(fmt::format("-l{}", lib));

            auto &linkOpts = targetInfo.linkerOptions;

            arguments.insert(arguments.end(), linkOpts.begin(), linkOpts.end());

            auto linkerResult = invokeLinker(linkerType, arguments);
            if (!linkerResult.empty()) {
                fmt::print("Module IR:\n");
                result->module->print(llvm::outs(), nullptr);
                fmt::print("\n");

                throw std::runtime_error(linkerResult);
            }
        }

        log(LogSource::targetDone, "Built target {}", name);

        auto &ref = *result;
        updatedTargets.insert({ target, std::move(result) });

        return ref;
    }

    ProjectManager::ProjectManager(const TargetConfig &main, const std::string &triple, const std::string &root)
        : mainTarget(main) // cannot std::move because i need the data later in constructor
        , builderTarget(triple)
        , sourceDatabase(managerCallback) {
        auto lockPath = fs::path(main.outputDirectory) / "build-lock.yaml";
        if (fs::exists(lockPath))
            lock = BuildLockFile(YAML::LoadFile(lockPath.string()));

        platform = Platform::byTriple(root, builderTarget.triple, lock);

        packageManager.emplace(*platform, main.packagesDirectory, root);

        targetCache.add(main, *packageManager);
    }

    ProjectManager::~ProjectManager() {
        // strange but i want to write lock before I leave

        auto text = lock.serialize();

        auto lockPath = fs::path(mainTarget.outputDirectory) / "build-lock.yaml";
        std::ofstream stream(lockPath);

        if (stream.is_open())
            stream << text;
    }
}
