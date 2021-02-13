#include <parser/type.h>

#include <parser/variable.h>

TypeNode::TypeNode(Node *parent) : Node(parent, Kind::Type) {
    match("type", true);

    name = token();

    needs("{");

    while (!end() && !peek("}")) {
        push<VariableNode>(false, false);

        next(",");
    }

    needs("}");
}
