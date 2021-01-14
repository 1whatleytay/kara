#pragma once

#include <parser/kinds.h>

struct BoolNode : public Node {
    bool value = false;

    explicit BoolNode(Node *parent);
};
