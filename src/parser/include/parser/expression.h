#pragma once

#include <parser/kinds.h>

#include <parser/unary.h>
#include <parser/operator.h>

#include <variant>

struct ExpressionNoun;
struct ExpressionOperation;
struct ExpressionCombinator;
using ExpressionResult = std::variant<ExpressionNoun, ExpressionOperation, ExpressionCombinator>;

struct ExpressionNoun {
    const Node *content = nullptr;

    std::vector<const Node *> modifiers;

    void push(const Node *node);
};

struct ExpressionOperation {
    std::unique_ptr<ExpressionResult> a;

    UnaryNode *op = nullptr;

    ExpressionOperation(std::unique_ptr<ExpressionResult> a, UnaryNode *op);
};

struct ExpressionCombinator {
    std::unique_ptr<ExpressionResult> a;
    std::unique_ptr<ExpressionResult> b;

    OperatorNode *op = nullptr;

    ExpressionCombinator(
        std::unique_ptr<ExpressionResult> a, std::unique_ptr<ExpressionResult> b, OperatorNode *op);
};

struct ExpressionNode : public Node {
    ExpressionResult result;

    explicit ExpressionNode(Node *parent);
};
