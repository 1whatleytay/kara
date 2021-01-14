#include <parser/bool.h>

BoolNode::BoolNode(Node *parent) : Node(parent, Kind::Bool) {
    value = select<bool>({ "false", "true" });
}
