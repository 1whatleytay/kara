#pragma once

#include <parser/kinds.h>

namespace kara::parser {
    struct Root : public hermes::Node {
        explicit Root(hermes::State &state, bool external = false);
    };
}
