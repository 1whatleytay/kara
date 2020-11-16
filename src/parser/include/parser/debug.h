#pragma once

#include <parser/kinds.h>

struct DebugNode : public Node {
    enum class Type {
        Expression,
        Reference,
    };

    Type type = Type::Expression;

    explicit DebugNode(Node *parent);
};
