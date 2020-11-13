#pragma once

#include <parser/kinds.h>

struct OperatorNode : public Node {
    enum class Operation {
        Add,
        Sub,
        Mul,
        Div,
        Equals,
        NotEquals,
        Greater,
        GreaterEqual,
        Lesser,
        LesserEqual,
        And,
        Or,
    };

    Operation op = Operation::Equals;

    explicit OperatorNode(Node *parent);
};
