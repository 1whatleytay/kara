#include <parser/null.h>

NullNode::NullNode(Node *parent) : Node(parent, Kind::Null) {
    match("null", true);
}
