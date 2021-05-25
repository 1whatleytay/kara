#include <parser/variable.h>

#include <parser/literals.h>
#include <parser/expression.h>

const Node *VariableNode::fixedType() const {
    return hasFixedType ? children[0].get() : nullptr;
}

const ExpressionNode *VariableNode::value() const {
    return hasInitialValue ? children[hasFixedType]->as<ExpressionNode>() : nullptr;
}

const NumberNode *VariableNode::constantValue() const {
    return hasConstantValue ? children[hasFixedType]->as<NumberNode>() : nullptr;
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
        if (parent && parent->is(Kind::Root) && push<NumberNode>(true)) {
            hasConstantValue = true;
        } else {
            push<ExpressionNode>();

            hasInitialValue = true;
        }
    } else {
        hasFixedType = true;
        pushTypename(this);

        if (next("external", true)) {
            isExternal = true;
        } else if (next("=")) {
            if (parent && parent->is(Kind::Root) && push<NumberNode>(true)) {
                hasConstantValue = true;
            } else {
                push<ExpressionNode>();

                hasInitialValue = true;
            }
        }
    }
}
