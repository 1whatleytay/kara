#include <parser/reference.h>

ReferenceNode::ReferenceNode(Node *parent) : Node(parent, Kind::Reference) {
    name = token();
}
