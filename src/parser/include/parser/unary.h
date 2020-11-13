#pragma once

#include <parser/kinds.h>

struct UnaryNode : public Node {
    enum class Operation {
        Not,
        At,
        Fetch,
    };

    Operation op = Operation::Not;

    explicit UnaryNode(Node *parent);
};
