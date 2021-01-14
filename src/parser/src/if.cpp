#include <parser/if.h>

#include <parser/scope.h>
#include <parser/expression.h>

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
