#pragma once

#include <cstdint>
#include <variant>

namespace kara::utils {
    enum class SpecialType {
        Any,
        Nothing,
        Null,
    };

    using NumberValue = std::variant<int64_t, uint64_t, double>;
}