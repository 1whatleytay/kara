#include <parser/index.h>

#include <parser/expression.h>

IndexNode::IndexNode(Node *parent) : Node(parent, Kind::Index) {
    match("[");

    push<ExpressionNode>();

    needs("]");
}
