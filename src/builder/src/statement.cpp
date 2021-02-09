#include <builder/builder.h>

#include <builder/error.h>

#include <parser/statement.h>
#include <parser/expression.h>

void BuilderScope::makeStatement(const StatementNode *node) {
    switch (node->op) {
        case StatementNode::Operation::Return: {
            if (node->children.empty()) {
                if (*function.type.returnType != TypenameNode::nothing) {
                    throw VerifyError(node,
                        "Method is of type {} but return statement does not return anything",
                        toString(*function.type.returnType));
                }
            } else {
                if (!node->children.empty() && *function.type.returnType == TypenameNode::nothing) {
                    throw VerifyError(node,
                        "Method does not have a return type but return statement returns value.");
                }

                // lambda :S
                BuilderResult resultRaw = makeExpression(node->children.front()->as<ExpressionNode>()->result);
                std::optional<BuilderResult> resultConverted = convert(resultRaw, *function.type.returnType);

                if (!resultConverted.has_value()) {
                    throw VerifyError(node,
                        "Cannot return {} from a function that returns {}.",
                        toString(resultRaw.type),
                        toString(*function.type.returnType));
                }

                BuilderResult result = std::move(*resultConverted);

                current.CreateStore(get(result), function.returnValue);
            }

            current.CreateBr(function.exitBlock);

            break;
        }

        default:
            assert(false);
    }
}
