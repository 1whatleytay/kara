#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

struct NumberNode : public Node {
    uint64_t value = 0;

    explicit NumberNode(Node *parent);
};
