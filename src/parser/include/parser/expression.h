#pragma once

#include <parser/kinds.h>

#include <utils/expression.h>

namespace kara::parser {
    struct Expression : public hermes::Node {
        utils::ExpressionResult result;

        explicit Expression(Node *parent, bool placeholder = false);
    };
}
