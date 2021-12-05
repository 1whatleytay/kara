#include <builder/library.h>

namespace kara::builder {
    std::optional<std::string> Library::match(const std::string &header) const {
        for (const auto &include : includes) {
            fs::path test = include / header;

            if (fs::exists(test)) {
                return test;
            }
        }

        return std::nullopt;
    }
}