#include <parser/function.h>

#include <parser/scope.h>
#include <parser/typename.h>
#include <parser/variable.h>
#include <parser/expression.h>

std::vector<const VariableNode *> FunctionNode::parameters() const {
    std::vector<const VariableNode *> result(parameterCount);

    for (size_t a = 0; a < parameterCount; a++)
        result[a] = children[a]->as<VariableNode>();

    return result;
}

const Node *FunctionNode::fixedType() const {
    return hasFixedType ? children[parameterCount].get() : nullptr;
}

const Node *FunctionNode::body() const {
    return isExtern ? nullptr : children[parameterCount + hasFixedType].get();
}

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
        pushTypename(this);
        hasFixedType = true;
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
