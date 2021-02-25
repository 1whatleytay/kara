#include <parser/ternary.h>

#include <parser/expression.h>

TernaryNode::TernaryNode(Node *parent) : Node(parent, Kind::Ternary) {
    match("?");

    push<ExpressionNode>();

    needs(":");

    push<ExpressionNode>();
}
