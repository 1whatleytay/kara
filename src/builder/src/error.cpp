#include <builder/error.h>

const char * VerifyError::what() const noexcept {
    return issue.c_str();
}

VerifyError::VerifyError(const Node *node, std::string message)
    : node(node), issue(std::move(message)) { }
