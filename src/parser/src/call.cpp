#include <parser/call.h>

#include <parser/expression.h>

CallNode::CallNode(Node *parent) : Node(parent, Kind::Call) {
    match("(");

    bool first = true;
    while (!end() && !peek(")")) {
        if (!first)
            needs(",");
        else
            first = false;

        push<ExpressionNode>();
    }

    needs(")");
}
