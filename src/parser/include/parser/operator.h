#pragma once

#include <parser/kinds.h>

struct ReferenceNode;
struct ExpressionNode;

struct AsNode : public Node {
    [[nodiscard]] const Node *type() const;

    explicit AsNode(Node *parent);
};

struct CallParameterNameNode : public Node {
    std::string name;

    explicit CallParameterNameNode(Node *parent);
};

struct CallNode : public Node {
    [[nodiscard]] std::vector<const ExpressionNode *> parameters() const;
    [[nodiscard]] std::unordered_map<size_t, const CallParameterNameNode *> names() const;

    [[nodiscard]] std::unordered_map<size_t, std::string> namesStripped() const;

    explicit CallNode(Node *parent);
};

struct DotNode : public Node {
    [[nodiscard]] const ReferenceNode *reference() const;

    explicit DotNode(Node *parent);
};

struct IndexNode : public Node {
    [[nodiscard]] const ExpressionNode *index() const;

    explicit IndexNode(Node *parent);
};

struct TernaryNode : public Node {
    [[nodiscard]] const ExpressionNode *onTrue() const;
    [[nodiscard]] const ExpressionNode *onFalse() const;

    explicit TernaryNode(Node *parent);
};

struct UnaryNode : public Node {
    enum class Operation {
        Not,
        Negative,
        Reference,
        Fetch,
    };

    Operation op = Operation::Not;

    explicit UnaryNode(Node *parent);
};

struct OperatorNode : public Node {
    enum class Operation {
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        Equals,
        NotEquals,
        GreaterEqual,
        LesserEqual,
        Greater,
        Lesser,
        And,
        Or,
    };

    Operation op = Operation::Equals;

    explicit OperatorNode(Node *parent);
};
