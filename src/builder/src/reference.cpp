#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/search.h>
#include <parser/type.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/reference.h>

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


const TypeNode *Builder::find(const StackTypename &type) {
    assert(type.node);

    auto match = [&type](const Node *node) {
        return node->is(Kind::Type) && node->as<TypeNode>()->name == type.value;
    };

    auto *result = search::exclusive::scope(type.node, match)->as<TypeNode>();

    if (!result)
        result = searchDependencies(match)->as<TypeNode>();

    return result;
}