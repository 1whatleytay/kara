#include <utils/expression.h>

namespace kara::utils {
    void ExpressionNoun::push(const hermes::Node *node) {
        // ??? - future taylor
        // why did I write it like this

        if (content)
            modifiers.push_back(node);
        else
            content = node;
    }

    ExpressionOperation::ExpressionOperation(std::unique_ptr<ExpressionResult> a, const hermes::Node *op)
        : a(std::move(a))
        , op(op) { }

    ExpressionCombinator::ExpressionCombinator(
        std::unique_ptr<ExpressionResult> a, std::unique_ptr<ExpressionResult> b, const parser::Operator *op)
        : a(std::move(a))
        , b(std::move(b))
        , op(op) { }
}