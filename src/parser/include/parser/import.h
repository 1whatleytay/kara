#pragma once

#include <parser/kinds.h>

struct ImportNode : public Node {
    std::string type;

    explicit ImportNode(Node *parent);
};
