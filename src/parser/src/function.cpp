#include <parser/function.h>

#include <parser/scope.h>
#include <parser/typename.h>
#include <parser/variable.h>
#include <parser/expression.h>

FunctionNode::FunctionNode(Node *parent) : Node(parent, Kind::Function) {
    name = token();

    if (next("(")) {
        while (!end() && !peek(")")) {
            push<VariableNode>(false, false);
            parameterCount++;

            next(",");
        }

        needs(")");
    }

    if (!(peek("{") || peek("=>"))) {
        returnType = std::move(pick<TypenameNode>()->type);
    }

    if (next("=>")) {
        push<ExpressionNode>();
    } else {
        match("{");

        push<CodeNode>();

        needs("}");
    }
}
