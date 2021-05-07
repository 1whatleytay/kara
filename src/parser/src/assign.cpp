#include <parser/assign.h>

#include <parser/expression.h>

AssignNode::AssignNode(Node *parent) : Node(parent, Kind::Assign) {
    push<ExpressionNode>();

    op = select<Operator>({ "=", "+=", "-=", "*=", "/=" });
    match();

    push<ExpressionNode>();
}
