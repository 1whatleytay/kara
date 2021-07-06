#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/type.h>
#include <parser/search.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/variable.h>

#include <unordered_set>

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

std::vector<const Node *> Builder::searchAllDependencies(const std::function<bool(Node *)> &match) {
    std::vector<const Node *> result;

    for (const ManagerFile *f : dependencies) {
        for (const auto &c : f->root->children) {
            if (match(c.get())) {
                result.push_back(c.get());
            }
        }
    }

    return result;
}

const Node *Builder::find(const ReferenceNode *node) {
    auto matchVariable = [node](const Node *value) -> bool {
        return (value->is(Kind::Variable) && value->as<VariableNode>()->name == node->name);
    };

    auto match = [node](const Node *value) -> bool {
        return (value->is(Kind::Variable) && value->as<VariableNode>()->name == node->name)
            || (value->is(Kind::Function) && value->as<FunctionNode>()->name == node->name)
            || (value->is(Kind::Type) && value->as<TypeNode>()->name == node->name);
    };

    const Node *result = search::exclusive::scopeFrom(node, matchVariable);

    if (!result)
        result = searchDependencies(match);

    if (!result)
        throw VerifyError(node, "Reference does not evaluate to anything.");

    return result;
}

std::vector<const Node *> Builder::findAll(const ReferenceNode *node) {
    auto matchVariable = [node](const Node *value) -> bool {
        return (value->is(Kind::Variable) && value->as<VariableNode>()->name == node->name);
    };

    auto match = [node](const Node *value) -> bool {
        return (value->is(Kind::Variable) && value->as<VariableNode>()->name == node->name)
            || (value->is(Kind::Function) && value->as<FunctionNode>()->name == node->name)
            || (value->is(Kind::Type) && value->as<TypeNode>()->name == node->name);
    };

    std::set<const Node *> unique;
    std::vector<const Node *> combine;

    auto add = [&unique, &combine](const std::vector<const Node *> &r) {
        for (auto k : r) {
            auto it = unique.find(k);

            if (it == unique.end()) {
                unique.insert(k);
                combine.push_back(k);
            }
        }
    };

    std::vector<const Node *> result = search::scopeFrom(node, matchVariable);
    std::vector<const Node *> more = searchAllDependencies(match);

    add(result);
    add(more);

    if (combine.empty())
        throw VerifyError(node, "Reference does not evaluate to anything.");

    return combine;
}
