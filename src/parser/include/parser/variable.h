#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

struct VariableNode : public Node {
    std::string name;
    Typename type;
    bool isMutable = false;

    explicit VariableNode(Node *parent, bool isExplicit = true);
};
