#pragma once

#include <parser/kinds.h>

#include <functional>

namespace search {
    using Checker = std::function<bool(const Node *)>;

    std::vector<const Node *> scope(const Node *origin, const Checker &checker);
}

namespace search::exclusive {
    const Node *scope(const Node *origin, const Checker &checker);
    const Node *parents(const Node *origin, const Checker &checker);
}
