#include <parser/variable.h>

#include <parser/expression.h>

VariableNode::VariableNode(Node *parent, bool isExplicit) : Node(parent, Kind::Variable) {
    std::vector<std::string> options = { "let", "var" };
    int mutability = select(options, false, !isExplicit);

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
