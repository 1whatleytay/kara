#include <parser/unary.h>

UnaryNode::UnaryNode(Node *parent) : Node(parent, Kind::Unary) {
    op = select<Operation>({
        "!", // Not
        "&", // Reference
        "@", // Fetch
    });
}
