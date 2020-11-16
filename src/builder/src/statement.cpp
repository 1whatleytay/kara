#include <builder/builder.h>

#include <builder/error.h>

#include <parser/statement.h>
#include <parser/expression.h>

void BuilderScope::makeStatement(const StatementNode *node) {
    switch (node->op) {
        case StatementNode::Operation::Return: {
            if (node->children.empty() && returnType != TypenameNode::nothing) {
                throw VerifyError(node,
                    "Method is of type {} but return statement does not return anything",
                    toString(returnType));
            }

            if (!node->children.empty() && returnType == TypenameNode::nothing) {
                throw VerifyError(node,
                    "Method does not return anything but return statement returns value.");
            }

            BuilderResult result = makeExpression(node->children.front()->as<ExpressionNode>()->result);

            current.CreateStore(get(result), function.returnValue);
            current.CreateBr(function.exitBlock);

            break;
        }

        default:
            assert(false);
    }
}
