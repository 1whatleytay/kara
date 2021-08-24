#pragma once

#include <parser/kinds.h>

#include <functional>

namespace kara::parser {
    namespace search {
        using Checker = std::function<bool(const hermes::Node *)>;

        std::vector<const hermes::Node *> scope(const hermes::Node *origin, const Checker &checker);
        std::vector<const hermes::Node *> scopeFrom(const hermes::Node *origin, const Checker &checker);
    }

    namespace search::exclusive {
        const hermes::Node *scope(const hermes::Node *origin, const Checker &checker);
        const hermes::Node *parents(const hermes::Node *origin, const Checker &checker);
        const hermes::Node *scopeFrom(const hermes::Node *origin, const Checker &checker);

        const hermes::Node *root(const hermes::Node *of);
    }
}
