#include <parser/function.h>

#include <parser/code.h>
#include <parser/typename.h>
#include <parser/variable.h>

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

    if (!peek("{")) {
        returnType = std::move(pick<TypenameNode>()->type);
    }

    match("{");

    push<CodeNode>();

    needs("}");
}
