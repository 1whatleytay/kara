#pragma once

#include <parser/kinds.h>

// Just for expression material.
struct ForInNode : public Node {
    explicit ForInNode(Node *parent);
};

struct ForNode : public Node {
    explicit ForNode(Node *parent);
};
