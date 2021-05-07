#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

struct FunctionNode : public Node {
    std::string name;

    size_t parameterCount = 0;
    Typename returnType = types::nothing();

    bool isExtern = false;

    explicit FunctionNode(Node *parent, bool external = false);
};
