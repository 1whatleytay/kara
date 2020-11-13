#pragma once

#include <parser/kinds.h>

struct ReferenceNode : public Node {
    std::string name;

    explicit ReferenceNode(Node *parent);
};
