#pragma once

#include <string>
#include <exception>

#include <hermes/node.h>

#include <fmt/format.h>

using namespace hermes;

class VerifyError : std::exception {
public:
    std::string issue;
    const Node *node = nullptr;

    [[nodiscard]] const char* what() const noexcept override;

    VerifyError(const Node *node, std::string message);

    template <typename ...Args>
    VerifyError(const Node *node, const char *format, Args... args)
        : VerifyError(node, fmt::format(format, args...)) { }
};
