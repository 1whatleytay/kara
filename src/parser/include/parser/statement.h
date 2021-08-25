#pragma once

#include <parser/kinds.h>

#include <parser/expression.h>

namespace kara::parser {
    struct Insight : public hermes::Node {
        [[nodiscard]] const Expression *expression();

        explicit Insight(Node *parent);
    };

    struct Statement : public hermes::Node {
        enum class Operation { Return, Break, Continue };

        explicit Statement(Node *parent);

        Operation op = Operation::Break;
    };
}
