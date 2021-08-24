#include <builder/error.h>

namespace kara::builder {
    const char *VerifyError::what() const noexcept {
        return issue.c_str();
    }

    VerifyError::VerifyError(const hermes::Node *node, std::string message)
        : node(node), issue(std::move(message)) { }
}
