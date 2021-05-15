#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

#include <optional>

struct VariableNode;

struct TypeNode : public Node {
    std::string name;

    bool isAlias = false;

    [[nodiscard]] const Node *alias() const;
    [[nodiscard]] std::vector<const VariableNode *> fields() const;

    explicit TypeNode(Node *parent, bool external = false);
};
