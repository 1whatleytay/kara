#include <builder/builder.h>

#include <builder/error.h>

#include <parser/statement.h>
#include <parser/expression.h>

void Builder::makeStatement(const StatementNode *node, const Scope &scope) {
    IRBuilder<> builder(scope.current);

    switch (node->op) {
        case StatementNode::Operation::Return: {
            if (node->children.empty() && scope.returnType != TypenameNode::nothing) {
                throw VerifyError(node,
                    "Method is of type {} but return statement does not return anything",
                    toString(scope.returnType));
            }

            if (!node->children.empty() && scope.returnType == TypenameNode::nothing) {
                throw VerifyError(node,
                    "Method does not return anything but return statement returns value.");
            }

            Result result = makeExpression(node->children.front()->as<ExpressionNode>()->result, scope);

            builder.CreateStore(result.get(builder), scope.returnValue);
            builder.CreateBr(scope.exit);

            break;
        }

        default:
            assert(false);
    }
}
