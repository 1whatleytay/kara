#include <parser/block.h>

#include <parser/scope.h>

BlockNode::BlockNode(Node *parent) : Node(parent, Kind::Block) {
    match("block", true);

    needs("{");

    push<CodeNode>();

    needs("}");
}
