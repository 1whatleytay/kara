#include <parser/scope.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/statement.h>
#include <parser/expression.h>

CodeNode::CodeNode(Node *parent) : Node(parent, Kind::Code) {
    while (!end() && !peek("}")) {
        push<BlockNode, IfNode, ForNode, StatementNode, AssignNode, VariableNode, ExpressionNode>();

        while (next(","));
    }
}

const CodeNode *BlockNode::body() const {
    return children.front()->as<CodeNode>();
}

BlockNode::BlockNode(Node *parent) : Node(parent, Kind::Block) {
    match("block", true);

    needs("{");

    push<CodeNode>();

    needs("}");
}

const ExpressionNode *IfNode::condition() const {
    return children.front()->as<ExpressionNode>();
}

const CodeNode *IfNode::onTrue() const {
    return children[1]->as<CodeNode>();
}

const Node *IfNode::onFalse() const {
    return children.size() >= 2 ? children[2].get() : nullptr;
}

IfNode::IfNode(Node *parent) : Node(parent, Kind::If) {
    match("if", true);

    push<ExpressionNode>();

    needs("{");

    push<CodeNode>();

    needs("}");

    if (next("else", true)) {
        if (next("{")) {
            push<CodeNode>();

            needs("}");
        } else {
            push<IfNode>(); // recursive :D
        }
    }
}

const VariableNode *ForInNode::name() const {
    return children[0]->as<VariableNode>();
}

const ExpressionNode *ForInNode::expression() const {
    return children[1]->as<ExpressionNode>();
}

ForInNode::ForInNode(Node *parent) : Node(parent, Kind::ForIn) {
    push<VariableNode>(false);

    match("in", true);

    push<ExpressionNode>();
}

const Node *ForNode::condition() const {
    return infinite ? nullptr : children[0].get();
}

const CodeNode *ForNode::body() const {
    return children[infinite]->as<CodeNode>();
}

ForNode::ForNode(Node *parent) : Node(parent, Kind::For) {
    match("for", true);

    if (!next("{")) {
        infinite = false;

        push<ForInNode, ExpressionNode>();

        needs("{");
    }

    push<CodeNode>();

    needs("}");
}
