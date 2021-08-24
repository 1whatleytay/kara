#pragma once

#include <parser/kinds.h>

namespace kara::parser {
    struct String;

    struct Import : public hermes::Node {
        std::string type;

        [[nodiscard]] const String *body() const;

        explicit Import(Node *parent);
    };
}
