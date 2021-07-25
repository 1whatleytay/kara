#include <parser/assign.h>

#include <parser/expression.h>

const ExpressionNode *AssignNode::left() const {
    return children[0]->as<ExpressionNode>();
}

const ExpressionNode *AssignNode::right() const {
    return children[1]->as<ExpressionNode>();
}

AssignNode::AssignNode(Node *parent) : Node(parent, Kind::Assign) {
    push<ExpressionNode>();

    op = select<Operator>({
        { "=", Operator::Assign },
        { "+=", Operator::Plus },
        { "-=", Operator::Minus },
        { "*=", Operator::Multiply },
        { "/=", Operator::Divide },
        { "%=", Operator::Modulo },
    });
    match();

    push<ExpressionNode>();
}
