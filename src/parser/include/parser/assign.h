#pragma once

#include <parser/kinds.h>

struct ExpressionNode;

struct AssignNode : public Node {
    enum class Operator {
        Assign,
        Plus,
        Minus,
        Multiply,
        Divide,
        Modulo
    };

    Operator op = Operator::Assign;

    [[nodiscard]] const ExpressionNode *left() const;
    [[nodiscard]] const ExpressionNode *right() const;

    explicit AssignNode(Node *parent);
};
