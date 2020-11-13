#include <builder/builder.h>

#include <builder/error.h>
#include <builder/search.h>

#include <parser/function.h>
#include <parser/variable.h>
#include <parser/reference.h>

const Node *Builder::find(const ReferenceNode *node) {
    const Node *result = search::exclusive::scope(node, [node](const Node *value) -> bool {
        return (value->is(Kind::Variable) && value->as<VariableNode>()->name == node->name)
            || (value->is(Kind::Function) && value->as<FunctionNode>()->name == node->name);
    });

    if (!result)
        throw VerifyError(node, "Reference does not evaluate to anything.");

    return result;
}
