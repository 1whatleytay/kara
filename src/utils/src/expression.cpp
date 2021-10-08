#include <utils/expression.h>

namespace kara::utils {
    ExpressionOperation::ExpressionOperation(std::unique_ptr<ExpressionResult> a, const hermes::Node *op)
        : a(std::move(a))
        , op(op) { }

    ExpressionCombinator::ExpressionCombinator(
        std::unique_ptr<ExpressionResult> a, std::unique_ptr<ExpressionResult> b, const parser::Operator *op)
        : a(std::move(a))
        , b(std::move(b))
        , op(op) { }
}