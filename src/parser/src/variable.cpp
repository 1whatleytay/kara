#include <parser/variable.h>

#include <parser/expression.h>

const Node *VariableNode::fixedType() const {
    return hasFixedType ? children[0].get() : nullptr;
}

const ExpressionNode *VariableNode::value() const {
    return children.size() > hasFixedType ? children[hasFixedType]->as<ExpressionNode>() : nullptr;
}

VariableNode::VariableNode(Node *parent, bool isExplicit, bool external) : Node(parent, Kind::Variable) {
    if (external)
        return;

    std::vector<std::string> options = { "let", "var" };
    size_t mutability = select(options, true, !isExplicit);

    if (mutability != options.size()) {
        match();
        isMutable = mutability;
    }

    name = token();

    if (next("=")) {
        push<ExpressionNode>();
    } else {
        hasFixedType = true;
        pushTypename(this);

        if (next("=")) {
            push<ExpressionNode>();
        }
    }
}
