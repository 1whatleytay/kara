#pragma once

#include <parser/kinds.h>

struct StatementNode : public Node {
    enum class Operation {
        Return,
        Break,
        Continue,
        // Skip
    };

    Operation op = Operation::Break;

    explicit StatementNode(Node *parent);
};
