#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

struct NumberNode : public Node {
    union {
        int64_t i = 0;
        uint64_t u;
        double f;
    } value;

    Typename type;

    explicit NumberNode(Node *parent);
};
