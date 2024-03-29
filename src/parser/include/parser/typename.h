#pragma once

#include <parser/kinds.h>

#include <utils/typename.h>

#include <variant>
#include <optional>

namespace kara::parser {
    struct Type;
    struct Number;
    struct Expression;

    struct NamedTypename : public hermes::Node {
        std::string name;

        explicit NamedTypename(Node *parent, bool external = false);
    };

    struct PrimitiveTypename : public hermes::Node {
        utils::PrimitiveType type = utils::PrimitiveType::Any;

        explicit PrimitiveTypename(Node *parent, bool external = false);
    };

    struct ReferenceTypename : public hermes::Node {
        utils::ReferenceKind kind = utils::ReferenceKind::Regular;

        bool isCPointer = false;
        bool isShared = false;
        std::optional<bool> isMutable;

        [[nodiscard]] const Node *body() const;

        explicit ReferenceTypename(Node *parent, bool external = false);
    };

    struct OptionalTypename : public hermes::Node {
        bool bubbles = false;

        [[nodiscard]] const Node *body() const;

        explicit OptionalTypename(Node *parent, bool external = false);
    };

    struct ArrayTypename : public hermes::Node {
        utils::ArrayKind type = utils::ArrayKind::VariableSize;

        [[nodiscard]] const Node *body() const;
        [[nodiscard]] const Number *fixedSize() const;
        [[nodiscard]] const Expression *variableSize() const;

        explicit ArrayTypename(Node *parent, bool external = false);
    };

    struct FunctionTypename : public hermes::Node {
        utils::FunctionKind kind = utils::FunctionKind::Regular;

        bool isLocked = false;

        [[nodiscard]] std::vector<const Node *> parameters() const;
        [[nodiscard]] const Node *returnType() const;

        explicit FunctionTypename(Node *parent, bool external = false);
    };

    void pushTypename(hermes::Node *parent);
}
