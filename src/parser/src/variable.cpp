#include <parser/variable.h>

VariableNode::VariableNode(Node *parent, bool isExplicit) : Node(parent, Kind::Variable) {
    std::vector<std::string> options = { "let", "var" };
    int mutability = select(options, false, !isExplicit);

    if (mutability != options.size()) {
        match();
        isMutable = mutability;
    }

    name = token();

    type = std::move(pick<TypenameNode>()->type);
}
