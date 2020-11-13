#pragma once

#include <parser/kinds.h>

struct RootNode : public Node {
    explicit RootNode(State &state);
};
