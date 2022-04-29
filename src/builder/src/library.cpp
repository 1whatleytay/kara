#include <builder/library.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace kara::builder {
    std::optional<std::string> Library::match(const std::string &header) const {
        for (const auto &include : includes) {
            fs::path test = fs::path(include) / header;

            if (fs::exists(test)) {
                return test;
            }
        }

        return std::nullopt;
    }
}