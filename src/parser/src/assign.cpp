#include <parser/assign.h>

#include <parser/expression.h>

AssignNode::AssignNode(Node *parent) : Node(parent, Kind::Assign) {
    push<ExpressionNode>();

    match("=");

    push<ExpressionNode>();
}
