#pragma once

#include <parser/kinds.h>

struct DebugNode : public Node {
    explicit DebugNode(Node *parent);
};
