#include <parser/parentheses.h>

#include <parser/expression.h>

ParenthesesNode::ParenthesesNode(Node *parent) : Node(parent, Kind::Parentheses) {
    match("(");

    push<ExpressionNode>();

    needs(")");
}
