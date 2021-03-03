#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

struct AsNode : public Node {
    Typename type;

    explicit AsNode(Node *parent);
};
