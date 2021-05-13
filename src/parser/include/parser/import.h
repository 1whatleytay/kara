#pragma once

#include <parser/kinds.h>

struct StringNode;

struct ImportNode : public Node {
    std::string type;

    const StringNode *body() const;

    explicit ImportNode(Node *parent);
};
