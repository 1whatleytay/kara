#pragma once

#include <parser/kinds.h>

struct UnaryNode : public Node {
    enum class Operation {
        Not,
        Reference,
        Fetch,
    };

    Operation op = Operation::Not;

    explicit UnaryNode(Node *parent);
};
