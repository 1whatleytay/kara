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

    std::string triple;

    bool printIR = false;
    bool interpret = false;

    Options() = default;
    Options(int count, const char **args);
};
