#include <cli/cli.h>

#include <cli/log.h>

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

    void CLICreateOptions::connect() { app->add_option("name", name, "Name of the project.")->required(); }

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

    void CLICleanOptions::connect() { app->add_option("-p,--project", projectFile, "Project file to use."); }

    void CLIRemoveOptions::connect() { }

    void CLIBuildOptions::connect() {
        app->add_option("target", target, "Target to build.");

        app->add_option("--triple", triple, "Triple to build for.");
        app->add_option("-l,--linker", linkerType, "Name of linker flavour to use.");
        app->add_option("-p,--project", projectFile, "Project file to use.");

        app->add_flag("--print-ir", printIr, "Whether or not to print generated IR.");
    }

    void CLICompileOptions::connect() { compileOptions.connect(*app); }

    void CLIExposeOptions::connect() {
        app->add_option("file", filePath, "The path of the file to expose.")->required();
        // app->add_option("--relative-to", relativeTo, "Optionally, a relative path to base the file path.");
        app->add_option("--type", type, "Type of file (kara, c, ...).");
        app->add_option("--target", target, "Target to use as reference point.");

        app->add_option("-p,--project", projectFile, "Project file to use.");
    }

    CLIOptions::CLIOptions(int count, const char **args) {
        CLI::App app;

        app.require_subcommand(1);

        auto root = count != 0 ? args[0] : "";

        auto hook = [&root, &app](CLIHook &hook, const char *name, const char *description) {
            hook.attach(app.add_subcommand(name, description), root); // ?
        };

        hook(create, "create", "Create a new project.");
        hook(install, "install", "Install an additional dependency.");
        hook(remove, "remove", "Remove an existing dependency.");
        hook(clean, "clean", "Remove all files in build folder.");
        hook(run, "run", "Run a target from this project directory.");
        hook(build, "build", "Build a target from this project directory.");
        hook(compile, "compile", "Invoke compiler on a single source file.");
        hook(expose, "expose", "Parse file and return structure data.");

        try {
            app.parse(count, args);
        } catch (const CLI::Error &e) { fmt::print("{}\n", app.help()); } catch (const std::runtime_error &e) {
            log(LogSource::error, "{}", e.what());
        }
    }
}
