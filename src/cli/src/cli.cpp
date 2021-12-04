#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

namespace kara::cli {
    void CLIHook::attach(CLI::App *newApp, std::string newRoot) {
        this->app = newApp;
        this->root = std::move(newRoot);

        connect();

        app->parse_complete_callback([this]() { execute(); });
    }

    void CLIAddOptions::connect() {
        app->add_option("url", url, "URL to a Kara/CMake package.")->required();
        app->add_option("--name", name, "URL to a Kara/CMake package.");
        app->add_option("-p,--project", projectFile, "Project file to use.");
        app->add_option("-a,--arg", arguments, "Arguments to pass to CMake build.");
        app->add_option("--no-write", noWrite, "If set, the main config file will not be updated.");
    }

    void CLIRunOptions::connect() {
        app->add_option("target", target, "Target to build.");

        app->add_option("--triple", triple, "Triple to build for.");
        app->add_option("-l,--linker", linkerType, "Name of linker flavour to use.");
        app->add_option("-p,--project", projectFile, "Project file to use.");
    }

    void CLICleanOptions::connect() {
        app->add_option("-p,--project", projectFile, "Project file to use.");
    }

    void CLIRemoveOptions::connect() { }

    void CLIBuildOptions::connect() {
        app->add_option("target", target, "Target to build.");

        app->add_option("--triple", triple, "Triple to build for.");
        app->add_option("-l,--linker", linkerType, "Name of linker flavour to use.");
        app->add_option("-p,--project", projectFile, "Project file to use.");

        app->add_flag("--print-ir", printIr, "Whether or not to print generated IR.");
    }

    void CLICompileOptions::connect() {
        compileOptions.connect(*app);
    }

    CLIOptions::CLIOptions(int count, const char **args) {
        CLI::App app;

        app.require_subcommand(1);

        auto root = count != 0 ? args[0] : "";

        auto hook = [&root](CLIHook &hook, CLI::App *app) {
            hook.attach(app, root); // ?
        };

        hook(install, app.add_subcommand("install", "Install an additional dependency."));
        hook(remove, app.add_subcommand("remove", "Remove an existing dependency."));
        hook(clean, app.add_subcommand("clean", "Remove all files in build folder."));
        hook(run, app.add_subcommand("run", "Run a target from this project directory."));
        hook(build, app.add_subcommand("build", "Build a target from this project directory."));
        hook(compile, app.add_subcommand("compile", "Invoke compiler on a single source file."));

        try {
            app.parse(count, args);
        } catch (const CLI::Error &e) {
            fmt::print("{}\n", app.help());
        } catch (const std::runtime_error &e) {
            log(LogSource::error, "{}", e.what());
        }
    }
}
