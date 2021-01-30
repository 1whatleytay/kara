#include <parser/debug.h>

#include <parser/typename.h>
#include <parser/reference.h>
#include <parser/expression.h>

DebugNode::DebugNode(Node *parent) : Node(parent, Kind::Debug) {
    match("debug", true);

    if (next("(")) {
        type = select<Type>({ "exp", "var", "return", "type" });

        needs(")");
    }

    switch (type) {
        case Type::Expression:
            push<ExpressionNode>();
            break;

        case Type::Type:
        case Type::Reference:
            push<ReferenceNode>();
            break;

        default:
            break;
    }
}
