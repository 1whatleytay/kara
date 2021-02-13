#pragma once

#include <parser/kinds.h>

struct TypeNode : public Node {
    std::string name;

    explicit TypeNode(Node *parent);
};
