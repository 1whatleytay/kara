#include <parser/search.h>

namespace kara::parser {
    namespace search {
        std::vector<const hermes::Node *> scope(const hermes::Node *origin, const Checker &checker) {
            std::vector<const hermes::Node *> result;

            hermes::Node *parent = origin->parent;

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

        std::vector<const hermes::Node *> scopeFrom(const hermes::Node *origin, const Checker &checker) {
            std::vector<const hermes::Node *> result;

            const hermes::Node *itself = origin;
            const hermes::Node *parent = origin->parent;

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
        const hermes::Node *scope(const hermes::Node *origin, const Checker &checker) {
            hermes::Node *parent = origin->parent;

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


        const hermes::Node *parents(const hermes::Node *origin, const Checker &checker) {
            const hermes::Node *parent = origin->parent;

            while (parent && !checker(parent)) {
                parent = parent->parent;
            }

            return parent;
        }

        const hermes::Node *scopeFrom(const hermes::Node *origin, const Checker &checker) {
            const hermes::Node *itself = origin;
            const hermes::Node *parent = origin->parent;

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


        const hermes::Node *root(const hermes::Node *of) {
            while (of && of->parent)
                of = of->parent;

            return of;
        }
    }
}
