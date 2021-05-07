#pragma once

#include <parser/kinds.h>

struct AssignNode : public Node {
    enum class Operator {
        Assign,
        Plus,
        Minus,
        Multiply,
        Divide
    };

    Operator op = Operator::Assign;

    explicit AssignNode(Node *parent);
};
