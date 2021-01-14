#pragma once

#include <parser/kinds.h>

struct IfNode : public Node {
    explicit IfNode(Node *parent);
};
