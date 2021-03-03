#include <parser/as.h>

AsNode::AsNode(Node *parent) : Node(parent, Kind::As) {
    match("as", true);

    type = pick<TypenameNode>()->type;
}
