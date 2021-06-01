#pragma once

#include <parser/kinds.h>

struct VariableNode;
struct ExpressionNode;

struct CodeNode : public Node {
    explicit CodeNode(Node *parent);
};

struct BlockNode : public Node {
    enum class Type {
        Regular,
        Exit
    };

    Type type = Type::Regular;

    [[nodiscard]] const CodeNode *body() const;

    explicit BlockNode(Node *parent);
};

struct IfNode : public Node {
    [[nodiscard]] const ExpressionNode *condition() const;

    [[nodiscard]] const CodeNode *onTrue() const;
    [[nodiscard]] const Node *onFalse() const;

    explicit IfNode(Node *parent);
};

// Just for expression material.
struct ForInNode : public Node {
    [[nodiscard]] const VariableNode *name() const;
    [[nodiscard]] const ExpressionNode *expression() const;

    explicit ForInNode(Node *parent);
};

struct ForNode : public Node {
    bool infinite = true;

    [[nodiscard]] const Node *condition() const;
    [[nodiscard]] const CodeNode *body() const;

    explicit ForNode(Node *parent);
};

