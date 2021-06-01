#pragma once

#include <set>
#include <string>

struct OptionsError : std::exception {
    std::string reason;

    [[nodiscard]] const char *what() const noexcept override;

    explicit OptionsError(std::string reason);
};

struct Options {
    std::set<std::string> inputs;
    std::string output;

    std::string triple;

    std::set<std::string> libraries;

    bool printIR = false;
    bool optimize = false;
    bool interpret = false;

    Options() = default;
    Options(int count, const char **args);
};
