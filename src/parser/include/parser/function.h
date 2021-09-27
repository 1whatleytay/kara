#pragma once

#include <parser/kinds.h>

#include <parser/typename.h>

#include <vector>

namespace kara::parser {
    struct Variable;

    struct Function : public hermes::Node {
        std::string name;

        size_t parameterCount = 0;

        bool isExtern = false;
        bool hasFixedType = false;

        bool isCVarArgs = false; // uh oh

        [[nodiscard]] std::vector<const Variable *> parameters() const;

        [[nodiscard]] const Node *fixedType() const;
        [[nodiscard]] const Node *body() const;

        explicit Function(Node *parent, bool external = false);
    };
}
