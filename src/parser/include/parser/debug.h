#pragma once

#include <parser/kinds.h>

struct DebugNode : public Node {
    enum class Type {
        Type
    };

    Type type = Type::Type;

    explicit DebugNode(Node *parent);
};
