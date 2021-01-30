#pragma once

#include <parser/kinds.h>

struct DebugNode : public Node {
    enum class Type {
        Expression,
        Reference,
        Return,
        Type
    };

    Type type = Type::Expression;

    explicit DebugNode(Node *parent);
};
