#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

#include <optional>

namespace kara::parser {
    struct Expression;

    struct Variable : public hermes::Node {
        std::string name;

        bool isMutable = false;
        bool hasFixedType = false;
        bool hasInitialValue = false;
        bool hasConstantValue = false;

        bool isExternal = false;

        [[nodiscard]] const Node *fixedType() const;
        [[nodiscard]] const Expression *value() const;
        [[nodiscard]] const Number *constantValue() const;

        explicit Variable(Node *parent, bool isExplicit = true, bool external = false);
    };
}
