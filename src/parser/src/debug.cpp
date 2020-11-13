#include <parser/debug.h>

#include <parser/expression.h>

DebugNode::DebugNode(Node *parent) : Node(parent, Kind::Debug) {
    match("debug", true);

    push<ExpressionNode>();
}
