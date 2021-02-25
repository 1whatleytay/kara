#include <options/options.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

const char *OptionsError::what() const noexcept {
    return reason.c_str();
}

OptionsError::OptionsError(std::string reason) : reason(std::move(reason)) { }

Options::Options(int count, const char **args) {
    CLI::App app("Kara Language");

    app.add_option("-i,--input", inputFile, "Input source file.")->required();
    auto outputOption = app.add_option("-o,--output", outputFile, "Output binary file.");
    app.add_option("-t,--triple", triple, "Target triple.");
    app.add_flag("--interpret", interpret, "Whether or not to interpret and run the code.")->excludes(outputOption);
    app.add_flag("--print-ir", printIR, "Whether or not to print resultant IR.");

    try {
        app.parse(count, args);
    } catch (const CLI::Error &e) {
        app.exit(e);
        throw OptionsError(e.get_name());
    }
}
