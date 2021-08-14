#include <parser/search.h>

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

    std::vector<const Node *> scopeFrom(const Node *origin, const Checker &checker) {
        std::vector<const Node *> result;

        const Node *itself = origin;
        const Node *parent = origin->parent;

        while (parent) {
            bool foundItself = false;

            for (auto a = static_cast<int64_t>(parent->children.size() - 1); a >= 0; a--) {
                auto ptr = parent->children[a].get();

                if (foundItself && checker(ptr))
                    result.push_back(ptr);

                if (ptr == itself)
                    foundItself = true;
            }

            itself = parent;
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

    const Node *scopeFrom(const Node *origin, const Checker &checker) {
        const Node *itself = origin;
        const Node *parent = origin->parent;

        while (parent) {
            bool foundItself = false;

            for (auto a = static_cast<int64_t>(parent->children.size() - 1); a >= 0; a--) {
                auto ptr = parent->children[a].get();

                if (foundItself && checker(ptr))
                    return ptr;

                if (ptr == itself)
                    foundItself = true;
            }

            itself = parent;
            parent = parent->parent;
        }

        return nullptr;
    }


    const Node *root(const Node *of) {
        while (of && of->parent)
            of = of->parent;

        return of;
    }

}
