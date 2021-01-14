#include <builder/builder.h>

#include <builder/error.h>

#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/reference.h>

void BuilderScope::makeAssign(const AssignNode *node) {
    BuilderResult destination = makeExpression(node->children.front()->as<ExpressionNode>()->result);
    BuilderResult source = makeExpression(node->children.back()->as<ExpressionNode>()->result);

    if (destination.type != source.type) {
        throw VerifyError(node, "Assignment of type {} to {} is not allowed.",
            toString(source.type), toString(destination.type));
    }

    if (destination.kind != BuilderResult::Kind::Reference) {
        throw VerifyError(node, "Left side of assign expression must be some variable or reference.");
    }

    assert(destination.lifetime && source.lifetime);

    if (std::holds_alternative<ReferenceTypename>(destination.type)) {
        std::vector<MultipleLifetime *> sourceLifetimes =
            expand({ source.lifetime.get() }, source.lifetimeDepth + 1, true);
        std::vector<MultipleLifetime *> destinationLifetimes =
            expand({ destination.lifetime.get() }, destination.lifetimeDepth + 1, true);

        for (auto dest : destinationLifetimes) {
            dest->clear();

            for (auto src : sourceLifetimes) {
                dest->insert(dest->begin(), src->begin(), src->end());
            }

            dest->simplify();
        }
    }

    current.CreateStore(get(source), destination.value);
}
