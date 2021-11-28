#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::builder {
    struct Library {
        std::string language;

        std::vector<fs::path> includes;
        std::vector<fs::path> libraries;
        std::vector<fs::path> dynamicLibraries;
        std::vector<std::string> arguments;

//        [[nodiscard]] std::string serialize() const;

        [[nodiscard]] std::optional<std::string> match(const std::string &header) const;

        Library() = default;
        explicit Library(const std::string &text, const fs::path &root);
    };
}