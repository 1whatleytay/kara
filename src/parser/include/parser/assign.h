#pragma once

#include <parser/kinds.h>

struct ExpressionNode;

struct AssignNode : public Node {
    enum class Operator {
        Assign,
        Plus,
        Minus,
        Multiply,
        Divide
    };

    Operator op = Operator::Assign;

    const ExpressionNode *left() const;
    const ExpressionNode *right() const;

    explicit AssignNode(Node *parent);
};
