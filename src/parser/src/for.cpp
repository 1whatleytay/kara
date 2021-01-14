#include <parser/for.h>

#include <parser/scope.h>
#include <parser/variable.h>
#include <parser/expression.h>

ForInNode::ForInNode(Node *parent) : Node(parent, Kind::ForIn) {
    push<VariableNode>(false);

    match("in", true);

    push<ExpressionNode>();
}

ForNode::ForNode(Node *parent) : Node(parent, Kind::For) {
    match("for", true);

    if (!next("{")) {
        push<ForInNode, ExpressionNode>();

        needs("{");
    }

    push<CodeNode>();

    needs("}");
}
