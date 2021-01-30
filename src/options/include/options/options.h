#pragma once

#include <string>

struct OptionsError : std::exception {
    std::string reason;

    [[nodiscard]] const char *what() const noexcept override;

    explicit OptionsError(std::string reason);
};

struct Options {
    std::string inputFile;
    std::string outputFile;

    bool printIR = false;
    bool noLifetimes = false;

    Options(int count, const char **args);
};
