#pragma once

#include <string>

struct Options {
    std::string inputFile;
    std::string outputFile;

    bool printIR = false;

    Options(int count, const char **args);
};
