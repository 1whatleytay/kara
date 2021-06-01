#pragma once

#include <parser/kinds.h>

#include <parser/expression.h>

struct InsightNode : public Node {
    [[nodiscard]] const ExpressionNode *expression();

    explicit InsightNode(Node *parent);
};

struct StatementNode : public Node {
    enum class Operation {
        Return,
        Break,
        Continue
    };

    explicit StatementNode(Node *parent);

    Operation op = Operation::Break;
};
