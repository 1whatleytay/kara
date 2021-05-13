#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/type.h>
#include <parser/search.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/variable.h>

const Node *Builder::searchDependencies(const std::function<bool(Node *)> &match) {
    for (const ManagerFile *f : dependencies) {
        for (const auto &c : f->root->children) {
            if (match(c.get())) {
                return c.get();
            }
        }
    }

    return nullptr;
}

const Node *Builder::find(const ReferenceNode *node) {
    auto match = [node](const Node *value) -> bool {
        return (value->is(Kind::Variable) && value->as<VariableNode>()->name == node->name)
            || (value->is(Kind::Function) && value->as<FunctionNode>()->name == node->name);
    };

    const Node *result = search::exclusive::scope(node, match);

    if (!result)
        result = searchDependencies(match);

    if (!result)
        throw VerifyError(node, "Reference does not evaluate to anything.");

    return result;
}
