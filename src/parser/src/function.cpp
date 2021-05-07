#include <parser/function.h>

#include <parser/scope.h>
#include <parser/typename.h>
#include <parser/variable.h>
#include <parser/expression.h>

FunctionNode::FunctionNode(Node *parent, bool external) : Node(parent, Kind::Function) {
    if (external)
        return;

    name = token();

    if (next("(")) {
        while (!end() && !peek(")")) {
            push<VariableNode>(false, false);
            parameterCount++;

            next(",");
        }

        needs(")");
    }

    if (!(peek("{") || peek("=>") || peek("external"))) {
        returnType = std::move(pick<TypenameNode>()->type);
    }

    if (next("external")) {
        match();
        isExtern = true;
    } else if (next("=>")) {
        match();
        push<ExpressionNode>();
    } else {
        match("{");

        push<CodeNode>();

        needs("}");
    }
}
