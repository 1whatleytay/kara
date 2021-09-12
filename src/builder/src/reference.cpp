#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/function.h>
#include <parser/literals.h>
#include <parser/search.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <unordered_set>

namespace kara::builder {
    const hermes::Node *Builder::searchDependencies(const SearchChecker &match) {
        for (const ManagerFile *f : dependencies) {
            for (const auto &c : f->root->children) {
                if (match(c.get())) {
                    return c.get();
                }
            }
        }

        return nullptr;
    }

    std::vector<const hermes::Node *> Builder::searchAllDependencies(const SearchChecker &match) {
        std::vector<const hermes::Node *> result;

        for (const ManagerFile *f : dependencies) {
            for (const auto &c : f->root->children) {
                if (match(c.get())) {
                    result.push_back(c.get());
                }
            }
        }

        return result;
    }

//    const hermes::Node *Builder::find(const parser::Reference *node) {
//        auto matchVariable = [node](const hermes::Node *value) -> bool {
//            return (value->is(parser::Kind::Variable) && value->as<parser::Variable>()->name == node->name);
//        };
//
//        auto match = [node](const hermes::Node *value) -> bool {
//            return (value->is(parser::Kind::Variable) && value->as<parser::Variable>()->name == node->name)
//                || (value->is(parser::Kind::Function) && value->as<parser::Function>()->name == node->name)
//                || (value->is(parser::Kind::Type) && value->as<parser::Type>()->name == node->name);
//        };
//
//        const hermes::Node *result = parser::search::exclusive::scopeFrom(node, matchVariable);
//
//        if (!result)
//            result = searchDependencies(match);
//
////        if (!result)
////            throw VerifyError(node, "Reference does not evaluate to anything.");
//
//        return result;
//    }

    std::vector<const hermes::Node *> Builder::findAll(const parser::Reference *node) {
        auto matchVariable = [node](const hermes::Node *value) -> bool {
            return (value->is(parser::Kind::Variable) && value->as<parser::Variable>()->name == node->name);
        };

        auto match = [node](const hermes::Node *value) -> bool {
            return (value->is(parser::Kind::Variable) && value->as<parser::Variable>()->name == node->name)
                || (value->is(parser::Kind::Function) && value->as<parser::Function>()->name == node->name)
                || (value->is(parser::Kind::Type) && value->as<parser::Type>()->name == node->name);
        };

        std::unordered_set<const hermes::Node *> unique;
        std::vector<const hermes::Node *> combine;

        auto add = [&unique, &combine](const std::vector<const hermes::Node *> &r) {
            for (auto k : r) {
                auto it = unique.find(k);

                if (it == unique.end()) {
                    unique.insert(k);
                    combine.push_back(k);
                }
            }
        };

        std::vector<const hermes::Node *> result = parser::search::scopeFrom(node, matchVariable);
        std::vector<const hermes::Node *> more = searchAllDependencies(match);

        add(result);
        add(more);

//        if (combine.empty())
//            throw VerifyError(node, "Reference does not evaluate to anything.");

        return combine;
    }
}
