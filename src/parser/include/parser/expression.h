#pragma once

#include <parser/kinds.h>

#include <utils/expression.h>

namespace kara::parser {
    struct Expression : public hermes::Node {
        utils::ExpressionResult result;

        // Just to keep it alive for result.
        std::unique_ptr<Node> postfix;

        explicit Expression(Node *parent, bool placeholder = false);
    };
}
