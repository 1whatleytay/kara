#pragma once

#include <parser/kinds.h>

#include <variant>
#include <vector>

namespace kara::parser {
    struct Expression;

    struct Parentheses : public hermes::Node {
        [[nodiscard]] const Expression *body() const;

        explicit Parentheses(Node *parent);
    };

    struct Special : public hermes::Node {
        enum class Type { Any, Nothing, Null };

        Type type = Type::Any;

        explicit Special(Node *parent);
    };

    struct Bool : public hermes::Node {
        bool value = false;

        explicit Bool(Node *parent);
    };

    struct Number : public hermes::Node {
        std::variant<int64_t, uint64_t, double> value;

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
