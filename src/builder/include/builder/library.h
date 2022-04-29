#pragma once

#include <string>
#include <vector>
#include <optional>

namespace kara::builder {
    struct Library {
        std::vector<std::string> includes;
        std::vector<std::string> arguments;

        [[nodiscard]] std::optional<std::string> match(const std::string &header) const;
    };
}