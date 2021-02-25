#pragma once

#include <parser/kinds.h>

struct TernaryNode : public Node {
    explicit TernaryNode(Node *parent);
};
