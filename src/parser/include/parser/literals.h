#pragma once

#include <parser/kinds.h>

#include <vector>
#include <variant>

struct ExpressionNode;

struct ParenthesesNode : public Node {
    const ExpressionNode *body() const;

    explicit ParenthesesNode(Node *parent);
};

struct SpecialNode : public Node {
    enum class Type {
        Any, Nothing, Null
    };

    Type type = Type::Any;

    explicit SpecialNode(Node *parent);
};

struct BoolNode : public Node {
    bool value = false;

    explicit BoolNode(Node *parent);
};

struct NumberNode : public Node {
    std::variant<int64_t, uint64_t, double> value;

    explicit NumberNode(Node *parent);
};

struct StringNode : public Node {
    std::vector<size_t> inserts;

    std::string text;

    explicit StringNode(Node *parent);
};

struct ArrayNode : public Node {
    std::vector<const ExpressionNode *> elements() const;

    explicit ArrayNode(Node *parent);
};

struct ReferenceNode : public Node {
    std::string name;

    explicit ReferenceNode(Node *parent);
};
