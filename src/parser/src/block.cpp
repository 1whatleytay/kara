#include <parser/block.h>

#include <parser/code.h>

BlockNode::BlockNode(Node *parent) : Node(parent, Kind::Block) {
    match("block", true);

    needs("{");

    push<CodeNode>();

    needs("}");
}
