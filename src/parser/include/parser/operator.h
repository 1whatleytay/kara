#pragma once

#include <parser/kinds.h>

#include <utils/expression.h>

namespace kara::parser {
    struct Reference;
    struct Expression;

    struct As : public hermes::Node {
        [[nodiscard]] const Node *type() const;

        explicit As(Node *parent);
    };

    struct CallParameterName : public hermes::Node {
        std::string name;

        explicit CallParameterName(Node *parent);
    };

    struct Call : public hermes::Node {
        [[nodiscard]] std::vector<const Expression *> parameters() const;
        [[nodiscard]] std::unordered_map<size_t, const CallParameterName *> names() const;

        [[nodiscard]] std::unordered_map<size_t, std::string> namesStripped() const;

        explicit Call(Node *parent);
    };

    struct Dot : public hermes::Node {
        [[nodiscard]] const Reference *reference() const;

        explicit Dot(Node *parent);
    };

    struct Index : public hermes::Node {
        [[nodiscard]] const Expression *index() const;

        explicit Index(Node *parent);
    };

    struct Ternary : public hermes::Node {
        [[nodiscard]] const Expression *onTrue() const;
        [[nodiscard]] const Expression *onFalse() const;

        explicit Ternary(Node *parent);
    };

    struct Slash : public hermes::Node {
        explicit Slash(Node *parent);
    };

    struct Unary : public hermes::Node {
        utils::UnaryOperation op = utils::UnaryOperation::Not;

        explicit Unary(Node *parent);
    };

    struct Operator : public hermes::Node {
        utils::BinaryOperation op = utils::BinaryOperation::Equals;

        explicit Operator(Node *parent);
    };
}
