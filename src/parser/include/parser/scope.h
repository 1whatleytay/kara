#pragma once

#include <parser/kinds.h>

struct VariableNode;
struct ExpressionNode;

struct CodeNode : public Node {
    explicit CodeNode(Node *parent);
};

struct BlockNode : public Node {
    const CodeNode *body() const;

    explicit BlockNode(Node *parent);
};

struct IfNode : public Node {
    const ExpressionNode *condition() const;

    const CodeNode *onTrue() const;
    const Node *onFalse() const;

    explicit IfNode(Node *parent);
};

// Just for expression material.
struct ForInNode : public Node {
    const VariableNode *name() const;
    const ExpressionNode *expression() const;

    explicit ForInNode(Node *parent);
};

struct ForNode : public Node {
    bool infinite = true;

    const Node *condition() const;
    const CodeNode *body() const;

    explicit ForNode(Node *parent);
};

