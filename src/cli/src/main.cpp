#include <options/options.h>

#include <builder/manager.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

#include <yaml-cpp/yaml.h>

#include <fmt/color.h>
#include <fmt/printf.h>

#include <lld/Common/Driver.h>

#include <optional>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

std::string invokeLinker(const std::string &linker, const std::vector<std::string> &arguments) {
    using LinkFunction = bool (*)(
        llvm::ArrayRef<const char *>, bool,
        llvm::raw_ostream &, llvm::raw_ostream &);

    std::vector<const char *> cstrings(arguments.size());
    std::transform(arguments.begin(), arguments.end(), cstrings.begin(), [](const auto &e) {
        return e.c_str();
    });

    auto size = static_cast<int>(cstrings.size());

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

struct TargetConfig {
    std::unordered_set<std::string> needs; // other targets
    std::unordered_set<std::string> files;
    std::unordered_set<std::string> libraries;
    std::unordered_set<std::string> external;

    std::vector<std::string> linkerOptions;

    kara::options::Options defaultOptions;

    explicit TargetConfig(const YAML::Node &node) {
        if (auto value = node["needs"]) {
            if (value.IsSequence()) {
                for (const auto &file : value) {
                    needs.insert(file.as<std::string>());
                }
            }
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

                if (auto v = value["print-ir"])
                    defaultOptions.printIR = v.as<bool>();
                if (auto v = value["optimize"])
                    defaultOptions.optimize = v.as<bool>();
                if (auto v = value["interpret"])
                    defaultOptions.interpret = v.as<bool>();

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

struct CLIRunOptions {

    void connect(CLI::App &app) {

    }
};

struct CLIBuildOptions {
    std::string target;
    std::string linkerType = "macho";
    std::string projectFile = "project.yaml";

    void connect(CLI::App &app, const std::string &root) {
        app.add_option("target", target, "Target to build.");

        app.add_option("-l,--linker", linkerType, "Name of linker flavour to use.");
        app.add_option("-p,--project", projectFile, "Project file to use.");

        app.parse_complete_callback([this, root]() {
            auto config = ProjectConfig::loadFrom(projectFile);

            if (!config) {
                auto path = fs::absolute(fs::path(projectFile)).string();

                throw std::runtime_error(fmt::format("Cannot find config file at {}.", path));
            }

            std::string targetToBuild = target;

            if (targetToBuild.empty())
                targetToBuild = config->defaultTarget;

            if (targetToBuild.empty())
                throw std::runtime_error("Target to build must be specified over command line.");

            auto mark = [](const std::string &text, fmt::color color) {
                fmt::print("[");
                fmt::print(fmt::fg(color), "{:<4}", text);
                fmt::print("] ");
            };

            mark("TAR", fmt::color::green_yellow);
            fmt::print(fmt::fg(fmt::color::teal), "Building target {}\n", targetToBuild);

            auto targetInfo = config->targets.find(targetToBuild);

            if (targetInfo == config->targets.end())
                throw std::runtime_error(fmt::format("Cannot find target {} in project file.", targetToBuild));

            auto &targetConfig = targetInfo->second;

            auto options = targetConfig.defaultOptions;

            fs::create_directories(config->outputDirectory);

            options.inputs.insert(targetConfig.files.begin(), targetConfig.files.end());
            options.output = (fs::path(config->outputDirectory) / fmt::format("{}.o", targetToBuild)).string();

            options.libraries.insert(targetConfig.external.begin(), targetConfig.external.end());

            mark("TAR", fmt::color::green_yellow);
            fmt::print("Building {}...\n", options.output);

            auto callback = [mark](kara::builder::ManagerCallbackReason reason,
                                const fs::path &path, const std::string &type) {
                using Reasons = kara::builder::ManagerCallbackReason;

                fmt::color color;
                std::string message = "TAR";

                if (type.empty() || type == "kara") {
                    message = "KARA";
                    color = fmt::color::orange;
                } else if (type == "c") {
                    message = "C";
                    color = fmt::color::orange_red;
                }

                switch (reason) {
                case Reasons::Parsing: {
                    mark(message, color);
                    fmt::print("Preparing file ");
                    fmt::print(fmt::emphasis::italic, "{}\n", path.string());
                    break;
                }

                case Reasons::Building: {
                    mark(message, color);
                    fmt::print("Building file ");
                    fmt::print(fmt::emphasis::italic, "{}\n", path.string());
                    break;
                }

                case Reasons::Cleanup: {
                    mark("TAR", fmt::color::green_yellow);
                    fmt::print("Cleaning up\n");
                }
                }
            };

            kara::builder::Manager manager(options, callback);

            auto linkOutput = (fs::path(config->outputDirectory) / fmt::format("{}", targetToBuild)).string();

            mark("TAR", fmt::color::green_yellow);
            fmt::print("Linking {}...\n", linkOutput);

            std::vector<std::string> arguments = {
                root,
                options.output,
                "-o",
                linkOutput,
                "-arch", "x86_64",
                "-lSystem", "-L/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib",
                "-syslibroot", "/Library/Developer/CommandLineTools/SDKs/MacOSX11.sdk",
                "-platform_version", "macos", "11.0.0", "11.0.0",
            };

            auto &linkOpts = targetConfig.linkerOptions;

            arguments.insert(arguments.end(), linkOpts.begin(), linkOpts.end());

            auto result = invokeLinker(linkerType, arguments);
            if (!result.empty())
                throw std::runtime_error(result);

            mark("TAR", fmt::color::green_yellow);
            fmt::print(fmt::fg(fmt::color::forest_green) | fmt::emphasis::bold,
                "Built target {}\n", targetToBuild);

            fmt::print("Done.\n");
        });
    }
};

struct CLICompileOptions {
    kara::options::Options compileOptions;

    void connect(CLI::App &app) {
        compileOptions.connect(app);

        app.parse_complete_callback([this]() {
            try {
                std::make_unique<kara::builder::Manager>(compileOptions);
            } catch (const std::exception &) {
                // ignore
            }
        });
    }
};

struct CLIOptions {
    CLIInstallOptions install;
    CLIRemoveOptions remove;
    CLIRunOptions run;
    CLIBuildOptions build;
    CLICompileOptions compile;

    CLIOptions(int count, const char **args) {
        CLI::App app;

        app.require_subcommand(1);

        auto root = count != 0 ? args[0] : "";

        install.connect(*app.add_subcommand("install", "Install an additional dependency."));
        remove.connect(*app.add_subcommand("remove", "Remove an existing dependency."));
        run.connect(*app.add_subcommand("run", "Run a target from this project directory."));
        build.connect(*app.add_subcommand("build", "Build a target from this project directory."), root);
        compile.connect(*app.add_subcommand("compile", "Invoke compiler on a single source file."));

        try {
            app.parse(count, args);
        } catch (const CLI::Error &e) {
            fmt::print("{}\n", app.help());
        } catch (const std::runtime_error &e) {
            fmt::print("{}\n", e.what());
        }
    }
};

int main(int count, const char **args) {
    std::make_unique<CLIOptions>(count, args);

    return 0;
}