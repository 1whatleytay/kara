#pragma once

#include <parser/kinds.h>

namespace kara::parser {
    struct Expression;

    struct Assign : public hermes::Node {
        enum class Operator { Assign, Plus, Minus, Multiply, Divide, Modulo };

        Operator op = Operator::Assign;

        [[nodiscard]] const Expression *left() const;
        [[nodiscard]] const Expression *right() const;

        explicit Assign(Node *parent);
    };
}
