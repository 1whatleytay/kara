#include <parser/operator.h>

#include <parser/typename.h>
#include <parser/literals.h>
#include <parser/expression.h>

const Node *AsNode::type() const {
    return children.front().get();
}

AsNode::AsNode(Node *parent) : Node(parent, Kind::As) {
    force = !select<bool>({ "as!", "as" }, true);
    match();

    pushTypename(this);
}

std::vector<const ExpressionNode *> CallNode::parameters() const {
    std::vector<const ExpressionNode *> result(children.size());

    for (size_t a = 0; a < children.size(); a++)
        result[a] = children[a]->as<ExpressionNode>();

    return result;
}

CallNode::CallNode(Node *parent) : Node(parent, Kind::Call) {
    match("(");

    bool first = true;
    while (!end() && !peek(")")) {
        if (!first)
            needs(",");
        else
            first = false;

        push<ExpressionNode>();
    }

    needs(")");
}

const ReferenceNode *DotNode::reference() const {
    return children.front()->as<ReferenceNode>();
}

DotNode::DotNode(Node *parent) : Node(parent, Kind::Dot) {
    match(".");

    push<ReferenceNode>();
}

const ExpressionNode *IndexNode::index() const {
    return children.front()->as<ExpressionNode>();
}

IndexNode::IndexNode(Node *parent) : Node(parent, Kind::Index) {
    match("[");

    push<ExpressionNode>();

    needs("]");
}

const ExpressionNode *TernaryNode::onTrue() const {
    return children[0]->as<ExpressionNode>();
}

const ExpressionNode *TernaryNode::onFalse() const {
    return children[1]->as<ExpressionNode>();
}

TernaryNode::TernaryNode(Node *parent) : Node(parent, Kind::Ternary) {
    match("?");

    push<ExpressionNode>();

    needs(":");

    push<ExpressionNode>();
}

UnaryNode::UnaryNode(Node *parent) : Node(parent, Kind::Unary) {
    op = select<Operation>({
        "!", // Not
        "&", // Reference
        "@", // Fetch
    });
}

OperatorNode::OperatorNode(Node *parent) : Node(parent, Kind::Operator) {
    std::vector<std::string> doNotCapture = { "+=", "-=", "*=", "/=" };

    if (select(doNotCapture, false, true) != doNotCapture.size())
        error("Operator cannot capture this text.");

    op = select<Operation>({
        "+", // Add
        "-", // Sub
        "*", // Mul
        "/", // Div
        "==", // Equals
        "!=", // NotEquals
        ">", // Greater
        ">=", // GreaterEqual
        "<", // Lesser
        "<=", // LesserEqual
        "&&", // And
        "||" // Or
    });
}
