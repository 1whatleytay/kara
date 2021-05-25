#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

#include <optional>

struct ExpressionNode;

struct VariableNode : public Node {
    std::string name;

    bool isMutable = false;
    bool hasFixedType = false;
    bool hasInitialValue = false;
    bool hasConstantValue = false;

    bool isExternal = false;

    [[nodiscard]] const Node *fixedType() const;
    [[nodiscard]] const ExpressionNode *value() const;
    [[nodiscard]] const NumberNode *constantValue() const;

    explicit VariableNode(Node *parent, bool isExplicit = true, bool external = false);
};
