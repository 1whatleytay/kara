#pragma once

#include <string>

struct Options {
    std::string inputFile;
    std::string outputFile;

    Options(int count, const char **args);
};
