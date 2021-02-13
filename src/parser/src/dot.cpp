#include <parser/dot.h>

#include <parser/reference.h>

DotNode::DotNode(Node *parent) : Node(parent, Kind::Dot) {
    match(".");

    push<ReferenceNode>();
}
