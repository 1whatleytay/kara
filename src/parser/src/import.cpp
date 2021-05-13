#include <parser/import.h>

#include <parser/literals.h>

const StringNode *ImportNode::body() const {
    return children.front()->as<StringNode>();
}

ImportNode::ImportNode(Node *parent) : Node(parent, Kind::Import) {
    match("import", true);

    if (next("(")) {
        type = token();

        needs(")");
    }

    push<StringNode>();

    assert(children.back()->as<StringNode>()->inserts.empty());
}
