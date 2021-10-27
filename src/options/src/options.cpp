#include <options/options.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

namespace kara::options {
    const char *OptionsError::what() const noexcept { return reason.c_str(); }

    OptionsError::OptionsError(std::string reason)
        : reason(std::move(reason)) { }

    void Options::connect(CLI::App &app) {
        app.add_option("-i,--input", inputs, "Input source files.")->required();
        auto outputOption = app.add_option("-o,--output", output, "Output binary files.");

        app.add_option("-t,--triple", triple, "Target triple.");

        app.add_flag("--optimize", optimize, "Whether or not to optimize LLVM ir with passes.");
        app.add_flag("--interpret", interpret, "Whether or not to interpret and run the code.")->excludes(outputOption);
        app.add_flag("--print-ir", printIR, "Whether or not to print resultant IR.");

        app.add_option("-l,--library", libraries, "JSON files describing libraries.");

        app.add_option("--malloc", malloc, "Name of malloc stub function to link against (i8 * (size_t)).");
        app.add_option("--free", free, "Name of free stub function to link against (void (i8 *)).");
        app.add_option("--realloc", realloc, "Name of realloc stub function to link against (i8 * (i8 *, size_t)).");

        app.add_flag("--mutable-globals", mutableGlobals, "Whether or not to enable mutable globals.");
    }

    Options::Options(int count, const char **args) {
        CLI::App app("Kara Compiler");

        connect(app);

        try {
            app.parse(count, args);
        } catch (const CLI::Error &e) {
            app.exit(e);
            throw OptionsError(e.get_name());
        }
    }
}
