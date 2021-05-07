#include <parser/variable.h>

#include <parser/expression.h>

VariableNode::VariableNode(Node *parent, bool isExplicit, bool external) : Node(parent, Kind::Variable) {
    if (external)
        return;

    std::vector<std::string> options = { "let", "var" };
    size_t mutability = select(options, false, !isExplicit);

    if (mutability != options.size()) {
        match();
        isMutable = mutability;
    }

    name = token();

    if (next("=")) {
        push<ExpressionNode>();
    } else {
        fixedType = pick<TypenameNode>()->type;

        if (next("=")) {
            push<ExpressionNode>();
        }
    }
}
