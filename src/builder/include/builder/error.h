#pragma once

#include <exception>
#include <string>

#include <hermes/node.h>

#include <fmt/format.h>

namespace kara::builder {
    struct VerifyError : public std::exception {
        std::string issue;
        const hermes::Node *node = nullptr;

        [[nodiscard]] const char *what() const noexcept override;

        VerifyError(const hermes::Node *node, std::string message);

        template <typename... Args>
        VerifyError(const hermes::Node *node, const char *format, Args... args)
            : VerifyError(node, fmt::format(format, args...)) { }
    };
}
