#pragma once

#include <parser/kinds.h>

#include <utils/literals.h>

#include <variant>
#include <vector>

namespace kara::parser {
    struct Expression;

    struct Parentheses : public hermes::Node {
        [[nodiscard]] const Expression *body() const;

        explicit Parentheses(Node *parent);
    };

    struct Special : public hermes::Node {
        utils::SpecialType type = utils::SpecialType::Any;

        explicit Special(Node *parent);
    };

    struct Bool : public hermes::Node {
        bool value = false;

        explicit Bool(Node *parent);
    };

    struct Number : public hermes::Node {
        utils::NumberValue value;

        explicit Number(Node *parent, bool external = false);
    };

    struct String : public hermes::Node {
        std::vector<size_t> inserts;

        std::string text;

        explicit String(Node *parent);
    };

    struct Array : public hermes::Node {
        [[nodiscard]] std::vector<const Expression *> elements() const;

        explicit Array(Node *parent);
    };

    struct Reference : public hermes::Node {
        std::string name;

        explicit Reference(Node *parent);
    };

    struct New : public hermes::Node {
        [[nodiscard]] const Node *type() const;

        explicit New(Node *parent);
    };
}
