#include <parser/array.h>

#include <parser/expression.h>

ArrayNode::ArrayNode(Node *parent) : Node(parent, Kind::Array) {
    match("[");

    while (!end() && !peek("]")) {
        push<ExpressionNode>();

        next(","); // optional ig
    }

    needs("]");
}
