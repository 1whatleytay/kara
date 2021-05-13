#pragma once

#include <parser/kinds.h>

struct ReferenceNode;
struct ExpressionNode;

struct AsNode : public Node {
    const Node *type() const;

    explicit AsNode(Node *parent);
};

struct CallNode : public Node {
    std::vector<const ExpressionNode *> parameters() const;

    explicit CallNode(Node *parent);
};

struct DotNode : public Node {
    const ReferenceNode *reference() const;

    explicit DotNode(Node *parent);
};

struct IndexNode : public Node {
    const ExpressionNode *index() const;

    explicit IndexNode(Node *parent);
};

struct TernaryNode : public Node {
    const ExpressionNode *onTrue() const;
    const ExpressionNode *onFalse() const;

    explicit TernaryNode(Node *parent);
};

struct UnaryNode : public Node {
    enum class Operation {
        Not,
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
        Equals,
        NotEquals,
        Greater,
        GreaterEqual,
        Lesser,
        LesserEqual,
        And,
        Or,
    };

    Operation op = Operation::Equals;

    explicit OperatorNode(Node *parent);
};
