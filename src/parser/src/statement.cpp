#include <parser/statement.h>

#include <parser/expression.h>

const ExpressionNode *InsightNode::expression() { return children.front()->as<ExpressionNode>(); }

InsightNode::InsightNode(Node *parent) : Node(parent, Kind::Insight) {
    match("insight", true);

    push<ExpressionNode>();
}

StatementNode::StatementNode(Node *parent) : Node(parent, Kind::Statement) {
    op = select<Operation>({ "return", "break", "continue" }, true);

    match();

    if (op == Operation::Return && !peek("}")) {
        push<ExpressionNode>();

        if (!peek("}"))
            error("Must have closing bracket after return statement.");
    }
}
