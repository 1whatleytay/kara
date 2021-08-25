#pragma once

#include <parser/kinds.h>

namespace kara::parser {
    struct Variable;
    struct Expression;

    struct Code : public hermes::Node {
        explicit Code(Node *parent);
    };

    struct Block : public hermes::Node {
        enum class Type { Regular, Exit };

        Type type = Type::Regular;

        [[nodiscard]] const Code *body() const;

        explicit Block(Node *parent);
    };

    struct If : public hermes::Node {
        [[nodiscard]] const Expression *condition() const;

        [[nodiscard]] const Code *onTrue() const;
        [[nodiscard]] const Node *onFalse() const;

        explicit If(Node *parent);
    };

    // Just for expression material.
    struct ForIn : public hermes::Node {
        [[nodiscard]] const Variable *name() const;
        [[nodiscard]] const Expression *expression() const;

        explicit ForIn(Node *parent);
    };

    struct For : public hermes::Node {
        bool infinite = true;

        [[nodiscard]] const Node *condition() const;
        [[nodiscard]] const Code *body() const;

        explicit For(Node *parent);
    };
}
