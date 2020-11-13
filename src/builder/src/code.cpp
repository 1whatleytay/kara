#include <builder/builder.h>

#include <parser/code.h>
#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/statement.h>

void Builder::makeCode(const CodeNode *node, Scope &scope) {
    for (const auto &child : node->children) {
        switch (child->is<Kind>()) {
            case Kind::Variable:
                makeLocalVariable(child->as<VariableNode>(), scope);
                break;
            case Kind::Assign:
                makeAssign(child->as<AssignNode>(), scope);
                break;
            case Kind::Statement:
                makeStatement(child->as<StatementNode>(), scope);
                break;
            case Kind::Block:
                makeCode(child->children.front()->as<CodeNode>(), scope);
                break;

            case Kind::Expression:
                makeExpression(child->as<ExpressionNode>()->result, scope);
                break;

            default:
                assert(false);
        }
    }
}
