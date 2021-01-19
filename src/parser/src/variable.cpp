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

    if (std::unique_ptr<TypenameNode> typeNode = pick<TypenameNode>(true))
        fixedType = std::move(typeNode->type);

    if (next("=")) {
        push<ExpressionNode>();
    } else if (!fixedType) {
        error("No fixed type or default value provided to variable.");
    }
}
