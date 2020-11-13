#include <builder/builder.h>

#include <builder/error.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/reference.h>

void Builder::makeAssign(const AssignNode *node, Scope &scope) {
    Result destination = makeExpression(node->children.front()->as<ExpressionNode>()->result, scope);
    Result source = makeExpression(node->children.back()->as<ExpressionNode>()->result, scope);

    if (destination.type != source.type) {
        throw VerifyError(node, "Assignment of type {} to {} is not allowed.",
            toString(source.type), toString(destination.type));
    }

    if (destination.kind != Result::Kind::Reference) {
        throw VerifyError(node, "Left side of assign expression must be some variable or reference.");
    }

    IRBuilder<> builder(scope.current);

    Value *variableValue = destination.value;
    builder.CreateStore(source.get(builder), variableValue);
}
