#include <parser/operator.h>

#include <parser/typename.h>
#include <parser/literals.h>
#include <parser/expression.h>

const Node *AsNode::type() const {
    return children.front().get();
}

AsNode::AsNode(Node *parent) : Node(parent, Kind::As) {
    match("as");

    pushTypename(this);
}

CallParameterNameNode::CallParameterNameNode(Node *parent) : Node(parent, Kind::CallParameterName) {
    name = token();

    match(":");
}

std::vector<const ExpressionNode *> CallNode::parameters() const {
    std::vector<const ExpressionNode *> result;

    for (const auto &c : children) {
        if (c->is(Kind::Expression)) {
            result.push_back(c->as<ExpressionNode>());
        }
    }

    return result;
}

std::unordered_map<size_t, const CallParameterNameNode *> CallNode::names() const {
    std::unordered_map<size_t, const CallParameterNameNode *> result;

    size_t index = 0;

    for (const auto &c : children) {
        if (c->is(Kind::Expression))
            index++;

        if (c->is(Kind::CallParameterName))
            result[index] = c->as<CallParameterNameNode>();
    }

    return result;
}

std::unordered_map<size_t, std::string> CallNode::namesStripped() const {
    std::unordered_map<size_t, std::string> result;

    auto v = names();

    for (const auto &c : v)
        result[c.first] = c.second->name;

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

        push<CallParameterNameNode>(true);
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
        { "!", Operation::Not },
        { "-", Operation::Negative },
        { "&", Operation::Reference },
        { "@", Operation::Fetch }
    });
}

OperatorNode::OperatorNode(Node *parent) : Node(parent, Kind::Operator) {
    std::vector<std::string> doNotCapture = { "+=", "-=", "*=", "/=", "%=" };

    if (maybe<bool>({ { "+=", true }, { "-=", true }, { "*=", true }, { "/=", true }, { "%=", true } }, false))
        error("Operator cannot capture this text.");

    op = select<Operation>({
        { "+", Operation::Add },
        { "-", Operation::Sub },
        { "*", Operation::Mul },
        { "/", Operation::Div },
        { "%", Operation::Mod },
        { "==", Operation::Equals },
        { "!=", Operation::NotEquals },
        { ">=", Operation::GreaterEqual },
        { "<=", Operation::LesserEqual },
        { ">", Operation::Greater },
        { "<", Operation::Lesser },
        { "&&", Operation::And },
        { "||", Operation::Or },
    });
}
