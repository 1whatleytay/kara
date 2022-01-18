#pragma once

#include <tuple>
#include <string>
#include <vector>
#include <utility>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::cli {
    // unix/posix, first is status code, second is stdout socket
    std::pair<int, int> invokeCLIWithSocket(
        const std::string &program,
        std::vector<std::string> arguments,
        const std::string &currentDirectory = "");

    std::pair<int, std::vector<uint8_t>> invokeCLIWithStdOut(
        const std::string &program,
        std::vector<std::string> arguments,
        const std::string &currentDirectory = "");

    int invokeCLI(
        const std::string &program,
        std::vector<std::string> arguments,
        const std::string &currentDirectory = "");
}
