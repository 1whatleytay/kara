#pragma once

#include <memory>
#include <variant>
#include <vector>

namespace hermes {
    struct Node;
}

namespace kara::parser {
    struct Unary;
    struct Operator;
}

namespace kara::utils {
    enum class UnaryOperation {
        Not,
        Negative,
        Reference,
        Fetch,
    };

    enum class BinaryOperation {
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

    struct ExpressionNoun;
    struct ExpressionOperation;
    struct ExpressionCombinator;
    using ExpressionResult = std::variant<ExpressionNoun, ExpressionOperation, ExpressionCombinator>;

    struct ExpressionNoun {
        const hermes::Node *content = nullptr;

        std::vector<const hermes::Node *> modifiers;

        void push(const hermes::Node *node);
    };

    struct ExpressionOperation {
        std::unique_ptr<ExpressionResult> a;

        const hermes::Node *op = nullptr;

        ExpressionOperation(std::unique_ptr<ExpressionResult> a, const hermes::Node *op);
    };

    struct ExpressionCombinator {
        std::unique_ptr<ExpressionResult> a;
        std::unique_ptr<ExpressionResult> b;

        const parser::Operator *op = nullptr;

        ExpressionCombinator(
            std::unique_ptr<ExpressionResult> a, std::unique_ptr<ExpressionResult> b, const parser::Operator *op);
    };
}
