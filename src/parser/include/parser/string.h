#pragma once

#include <parser/kinds.h>

struct StringNode : public Node {
    std::vector<size_t> inserts;

    std::string text;

    explicit StringNode(Node *parent);
};
