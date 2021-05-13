#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

#include <vector>

struct VariableNode;

struct FunctionNode : public Node {
    std::string name;

    size_t parameterCount = 0;

    bool isExtern = false;
    bool hasFixedType = false;

    std::vector<const VariableNode *> parameters() const;

    const Node *fixedType() const;
    const Node *body() const;

    explicit FunctionNode(Node *parent, bool external = false);
};
