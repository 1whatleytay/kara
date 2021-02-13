#pragma once

#include <parser/kinds.h>

struct DotNode : public Node {
    explicit DotNode(Node *parent);
};
