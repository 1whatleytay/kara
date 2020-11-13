#include <builder/search.h>

namespace search {
    std::vector<const Node *> scope(const Node *origin, const Checker &checker) {
        std::vector<const Node *> result;

        Node *parent = origin->parent;

        while (parent) {
            for (const auto &child : parent->children) {
                if (checker(child.get())) {
                    result.push_back(child.get());
                }
            }

            parent = parent->parent;
        }

        return result;
    }
}

namespace search::exclusive {
    const Node *scope(const Node *origin, const Checker &checker) {
        Node *parent = origin->parent;

        while (parent) {
            for (const auto &child : parent->children) {
                if (checker(child.get())) {
                    return child.get();
                }
            }

            parent = parent->parent;
        }

        return nullptr;
    }


    const Node *parents(const Node *origin, const Checker &checker) {
        const Node *parent = origin->parent;

        while (parent && !checker(parent)) {
            parent = parent->parent;
        }

        return parent;
    }
}
