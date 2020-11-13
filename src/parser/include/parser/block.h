#pragma once

#include <parser/kinds.h>

struct BlockNode : public Node {
    explicit BlockNode(Node *parent);
};
