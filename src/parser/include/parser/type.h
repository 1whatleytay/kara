#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

#include <optional>

namespace kara::parser {
    struct Variable;

    struct Type : public hermes::Node {
        std::string name;

        bool isAlias = false;

        [[nodiscard]] const Node *alias() const;
        [[nodiscard]] std::vector<const Variable *> fields() const;

        explicit Type(Node *parent, bool external = false);
    };
}
