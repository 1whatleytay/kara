#include <options/options.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

namespace kara::options {
    const char *OptionsError::what() const noexcept { return reason.c_str(); }

    OptionsError::OptionsError(std::string reason)
        : reason(std::move(reason)) { }

    bool Options::operator==(const Options &other) const {
        return triple == other.triple
            && malloc == other.malloc
            && free == other.free
            && realloc == other.realloc
            && rawPlatform == other.rawPlatform
            && mutableGlobals == other.mutableGlobals;
    }

    bool Options::operator!=(const Options &other) const {
        return !operator==(other);
    }

    void Options::connect(CLI::App &app) {
//        app.add_option("-i,--input", inputs, "Input source files.")->required();
//        auto outputOption = app.add_option("-o,--output", output, "Output binary files.");

        app.add_option("-t,--triple", triple, "Target triple.");

//        app.add_flag("--optimize", optimize, "Whether or not to optimize LLVM ir with passes.");
//        app.add_flag("--interpret", interpret, "Whether or not to interpret and run the code.")->excludes(outputOption);
//        app.add_flag("--print-ir", printIR, "Whether or not to print resultant IR.");

//        app.add_option("-l,--library", libraries, "JSON files describing libraries.");

        app.add_option("--malloc", malloc, "Name of malloc stub function to link against (i8 * (size_t)).");
        app.add_option("--free", free, "Name of free stub function to link against (void (i8 *)).");
        app.add_option("--realloc", realloc, "Name of realloc stub function to link against (i8 * (i8 *, size_t)).");

        app.add_flag("--raw-platform", rawPlatform, "Disable any special handling for target platforms in build.");
        app.add_flag("--mutable-globals", mutableGlobals, "Whether or not to enable mutable globals.");
    }

    // this is bad without nullopt
    void Options::merge(const Options &other) {
        options::Options defaultOptions;

        // these should probably at least be a template function...
        if (other.triple != defaultOptions.triple)
            triple = other.triple;

        if (other.malloc != defaultOptions.malloc)
            malloc = other.malloc;
        if (other.free != defaultOptions.free)
            free = other.free;
        if (other.realloc != defaultOptions.realloc)
            realloc = other.realloc;

        if (other.rawPlatform != defaultOptions.rawPlatform)
            rawPlatform = other.rawPlatform;
        if (other.mutableGlobals != defaultOptions.mutableGlobals)
            mutableGlobals = other.mutableGlobals;
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
