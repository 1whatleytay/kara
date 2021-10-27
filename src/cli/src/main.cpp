#include <options/options.h>

#include <builder/error.h>
#include <builder/builder.h>
#include <builder/manager.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

#include <yaml-cpp/yaml.h>

#include <fmt/color.h>
#include <fmt/printf.h>

#include <lld/Common/Driver.h>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>

#include <optional>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include <unistd.h>

namespace fs = std::filesystem;

enum class TargetType {
    Library,
    Executable,
    Interface,
};

struct TargetConfig {
    TargetType type = TargetType::Library;

    std::unordered_set<std::string> files;
    std::unordered_set<std::string> libraries;
    std::unordered_set<std::string> external;

    std::vector<std::string> linkerOptions;

    kara::options::Options defaultOptions;

    explicit TargetConfig(const YAML::Node &node) {
        if (auto value = node["type"]) {
            std::unordered_map<std::string, TargetType> targetMap = {
                { "library", TargetType::Library },
                { "executable", TargetType::Executable },
                { "interface", TargetType::Interface },
            };

            auto it = targetMap.find(value.as<std::string>());
            if (it == targetMap.end())
                fmt::print("Warning: unknown target type {}.", value.as<std::string>());
            else
                type = it->second;
        }

        if (auto value = node["files"]) {
            if (value.IsSequence()) {
                for (const auto &file : value) {
                    files.insert(file.as<std::string>());
                }
            }
        }

        if (auto value = node["external"]) {
            if (value.IsSequence()) {
                for (const auto &file : value) {
                    external.insert(file.as<std::string>());
                }
            }
        }

        if (auto value = node["libraries"]) {
            if (value.IsSequence()) {
                for (const auto &library : value) {
                    libraries.insert(library.as<std::string>());
                }
            }
        }

        if (auto value = node["linker-options"])
            linkerOptions = value.as<std::vector<std::string>>();

        if (auto value = node["options"]) {
            if (value.IsMap()) {
                if (auto v = value["triple"])
                    defaultOptions.triple = v.as<std::string>();

//                if (auto v = value["print-ir"])
//                    defaultOptions.printIR = v.as<bool>();
//                if (auto v = value["optimize"])
//                    defaultOptions.optimize = v.as<bool>();
//                if (auto v = value["interpret"])
//                    defaultOptions.interpret = v.as<bool>();

                if (auto v = value["malloc"])
                    defaultOptions.malloc = v.as<std::string>();
                if (auto v = value["free"])
                    defaultOptions.free = v.as<std::string>();
                if (auto v = value["realloc"])
                    defaultOptions.realloc = v.as<std::string>();

                if (auto v = value["mutable-globals"])
                    defaultOptions.mutableGlobals = v.as<bool>();
            }
        }
    }
};

struct ProjectConfig {
    std::string defaultTarget;
    std::string outputDirectory = "build";

    std::unordered_map<std::string, TargetConfig> targets;

    static std::optional<ProjectConfig> loadFrom(const std::string &path) {
        std::ifstream stream(path);

        if (!stream.is_open())
            return std::nullopt;

        return ProjectConfig(YAML::Load(stream));
    }

    explicit ProjectConfig(const YAML::Node &node) {
        if (auto value = node["default"])
            defaultTarget = value.as<std::string>();

        if (auto value = node["output"])
            outputDirectory = value.as<std::string>();

        if (auto value = node["targets"]) {
            if (value.IsMap()) {
                for (const auto &target : value) {
                    targets.insert({ target.first.as<std::string>(), TargetConfig(target.second) });
                }
            }
        }
    }
};

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


void managerLogMark(const std::string &text, fmt::color color) {
    fmt::print("[");
    fmt::print(fmt::fg(color), "{:<4}", text);
    fmt::print("] ");
};

void managerCallback(const fs::path &path, const std::string &type) {
    fmt::color color = fmt::color::green_yellow;
    std::string message = "TAR";

    if (type.empty() || type == "kara") {
        message = "KARA";
        color = fmt::color::orange;
    } else if (type == "c") {
        message = "C";
        color = fmt::color::orange_red;
    }

    managerLogMark(message, color);
    fmt::print("Preparing file ");
    fmt::print(fmt::emphasis::italic, "{}\n", path.string());
}

struct TargetResult {
    std::unordered_set<std::string> libraries;
    std::unordered_set<std::string> external;

    std::vector<std::string> linkerOptions;

    kara::options::Options defaultOptions;

    std::unique_ptr<llvm::Module> module;
};

struct ProjectManager {
    ProjectConfig config;
    kara::builder::Manager manager;

    std::unordered_map<std::string, std::unique_ptr<TargetResult>> updatedTargets;

    fs::path createTargetDirectory(const std::string &target) const { // NOLINT(modernize-use-nodiscard)
        fs::path directory = fs::path(config.outputDirectory) / target;

        if (!fs::is_directory(directory))
            fs::create_directories(directory);

        return directory;
    }

    const TargetResult &makeTarget(
        const std::string &target, const std::string &root, const std::string &linkerType = "") {
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

        managerLogMark("TAR", fmt::color::green_yellow);
        fmt::print(fmt::fg(fmt::color::teal), "Building target {}\n", target);

        auto &options = result->defaultOptions;

        auto directory = createTargetDirectory(target);
        auto outputFile = directory / fmt::format("{}.o", target);

        managerLogMark("TAR", fmt::color::green_yellow);
        fmt::print("Building {}\n", outputFile.string());

        // this isn't really caring for order, its just adding whatever no matter the target
        // not really desired but I don't have the brain power to think of a better system
        for (const auto &library : result->external)
            manager.add(fs::path(library));

        std::vector<std::unique_ptr<llvm::Module>> modules;
        for (const auto &file : targetConfig.files) {
            auto &managerFile = manager.get(file);

            try {
                kara::builder::Builder builder { managerFile, options };

                fmt::color color = fmt::color::green_yellow;
                std::string message = "TAR";

                if (managerFile.type.empty() || managerFile.type == "kara") {
                    message = "KARA";
                    color = fmt::color::orange;
                } else if (managerFile.type == "c") {
                    message = "C";
                    color = fmt::color::orange_red;
                }

                managerLogMark(message, color);
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

        managerLogMark("TAR", fmt::color::green_yellow);
        fmt::print("Writing {}\n", outputFile.string());

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

            managerLogMark("TAR", fmt::color::green_yellow);
            fmt::print("Linking {}\n", linkFile.string());

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

        managerLogMark("TAR", fmt::color::green_yellow);
        fmt::print(fmt::fg(fmt::color::forest_green) | fmt::emphasis::bold, "Built target {}\n", target);

        auto &ref = *result;
        updatedTargets.insert({ target, std::move(result) });

        return ref;
    }

    ProjectManager(ProjectConfig config, const std::string &triple)
        : config(std::move(config))
        , manager(triple, managerCallback) { }
};

struct CLICreateOptions {
    void connect(CLI::App &app) {

    }
};

struct CLIInstallOptions {

    void connect(CLI::App &app) {

    }
};

struct CLIRemoveOptions {

    void connect(CLI::App &app) {

    }
};

struct CLICleanOptions {
    std::string projectFile = "project.yaml";

    void connect(CLI::App &app) {
        app.add_option("-p,--project", projectFile, "Project file to use.");

        app.parse_complete_callback([this]() {
            auto config = ProjectConfig::loadFrom(projectFile);

            if (!config) {
                auto path = fs::absolute(fs::path(projectFile)).string();

                throw std::runtime_error(fmt::format("Cannot find config file at {}.", path));
            }

            managerLogMark("TAR", fmt::color::green_yellow);
            fmt::print("Cleaned\n");

            fs::remove_all(config->outputDirectory);
        });
    }
};

struct CLIRunOptions {
    std::string target;
    std::string triple;
    std::string linkerType = "macho";
    std::string projectFile = "project.yaml";

    void connect(CLI::App &app, const std::string &root) {
        app.add_option("target", target, "Target to build.");

        app.add_option("--triple", triple, "Triple to build for.");
        app.add_option("-l,--linker", linkerType, "Name of linker flavour to use.");
        app.add_option("-p,--project", projectFile, "Project file to use.");

        app.parse_complete_callback([this, root]() {
            auto config = ProjectConfig::loadFrom(projectFile);

            if (!config) {
                auto path = fs::absolute(fs::path(projectFile)).string();

                throw std::runtime_error(fmt::format("Cannot find config file at {}.", path));
            }

            ProjectManager manager(*config, triple);

            std::string targetToBuild = target;

            if (targetToBuild.empty())
                targetToBuild = config->defaultTarget;

            if (targetToBuild.empty())
                throw std::runtime_error("Target to build must be specified over command line.");

            auto it = config->targets.find(targetToBuild);
            if (it == config->targets.end())
                throw std::runtime_error(fmt::format("Cannot find target {} in project file.", target));

            if (it->second.type != TargetType::Executable)
                throw std::runtime_error(fmt::format("Target {} does not have executable type.", target));

            manager.makeTarget(targetToBuild, root, linkerType);

            auto directory = manager.createTargetDirectory(targetToBuild);
            auto executable = directory / target; // ?

            managerLogMark("TAR", fmt::color::green_yellow);
            fmt::print("Running {}\n", targetToBuild);

            std::vector<char *const> arguments = { nullptr };
            execv(executable.string().c_str(), arguments.data());
        });
    }
};

struct CLIBuildOptions {
    std::string target;
    std::string triple;
    std::string linkerType = "macho";
    std::string projectFile = "project.yaml";

    void connect(CLI::App &app, const std::string &root) {
        app.add_option("target", target, "Target to build.");

        app.add_option("--triple", triple, "Triple to build for.");
        app.add_option("-l,--linker", linkerType, "Name of linker flavour to use.");
        app.add_option("-p,--project", projectFile, "Project file to use.");

        app.parse_complete_callback([this, root]() {
            auto config = ProjectConfig::loadFrom(projectFile);

            if (!config) {
                auto path = fs::absolute(fs::path(projectFile)).string();

                throw std::runtime_error(fmt::format("Cannot find config file at {}.", path));
            }

            ProjectManager manager(*config, triple);

            std::string targetToBuild = target;

            if (targetToBuild.empty())
                targetToBuild = config->defaultTarget;

            if (targetToBuild.empty())
                throw std::runtime_error("Target to build must be specified over command line.");

            manager.makeTarget(targetToBuild, root, linkerType);
        });
    }
};

struct CLICompileOptions {
    kara::options::Options compileOptions;

    void connect(CLI::App &app) {
        compileOptions.connect(app);

        app.parse_complete_callback([this]() {
            throw;

            try {
//                std::make_unique<kara::builder::Manager>(compileOptions);
            } catch (const std::exception &) {
                // ignore
            }
        });
    }
};

struct CLIOptions {
    CLIInstallOptions install;
    CLIRemoveOptions remove;
    CLICleanOptions clean;
    CLIRunOptions run;
    CLIBuildOptions build;
    CLICompileOptions compile;

    CLIOptions(int count, const char **args) {
        CLI::App app;

        app.require_subcommand(1);

        auto root = count != 0 ? args[0] : "";

        install.connect(*app.add_subcommand("install", "Install an additional dependency."));
        remove.connect(*app.add_subcommand("remove", "Remove an existing dependency."));
        clean.connect(*app.add_subcommand("clean", "Remove all files in build folder."));
        run.connect(*app.add_subcommand("run", "Run a target from this project directory."), root);
        build.connect(*app.add_subcommand("build", "Build a target from this project directory."), root);
        compile.connect(*app.add_subcommand("compile", "Invoke compiler on a single source file."));

        try {
            app.parse(count, args);
        } catch (const CLI::Error &e) {
            fmt::print("{}\n", app.help());
        } catch (const std::runtime_error &e) {
            fmt::print(fmt::fg(fmt::color::orange_red), "[ERROR] {}\n", e.what());
        }
    }
};

int main(int count, const char **args) {
    std::make_unique<CLIOptions>(count, args);

    return 0;
}