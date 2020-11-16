#include <options/options.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

Options::Options(int count, const char **args) {
    CLI::App app("Kara Language");

    app.add_option("-i,--input", inputFile, "Input source file.")->required();
    app.add_option("-o,--output", outputFile, "Output binary file.");
    app.add_flag("--print-ir", printIR, "Whether or not to print resultant IR.");

    try {
        app.parse(count, args);
    } catch (const CLI::Error &e) {
        app.exit(e);
        throw;
    }
}
