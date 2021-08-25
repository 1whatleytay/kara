#include <parser/expression.h>

#include <parser/literals.h>
#include <parser/operator.h>

#include <stdexcept>

namespace kara::parser {
    static utils::ExpressionResult applyUnary(
        const utils::ExpressionNoun &current, const std::vector<parser::Unary *> &unary) {
        utils::ExpressionResult value = current;

        for (auto a = unary.rbegin(); a < unary.rend(); a++)
            value = utils::ExpressionOperation(std::make_unique<utils::ExpressionResult>(std::move(value)), *a);

        return value;
    }

    Expression::Expression(Node *parent, bool placeholder)
        : Node(parent, Kind::Expression) {
        if (placeholder)
            return;

        bool exit = false;

        while (!end() && !exit) {
            while (push<Unary>(true))
                ;

            push<Parentheses, Array, String, Special, Bool, Number, New, Reference>();

            while (true) {
                if (!push<Call, Index, Dot, Operator>(true)) {
                    exit = true;
                    break;
                }

                if (children.back()->is(Kind::Operator))
                    break;
            }
        }

        // Calculate result (operator precedence).
        std::vector<utils::ExpressionResult> results;
        std::vector<Operator *> operators;

        {
            std::vector<Unary *> unary;
            utils::ExpressionNoun current;

            for (const auto &child : children) {
                if (child->is(Kind::Operator)) {
                    operators.push_back(child->as<Operator>());

                    results.emplace_back(applyUnary(current, unary));

                    unary.clear();
                    current = {};
                } else if (child->is(Kind::Unary)) {
                    unary.push_back(child->as<Unary>());
                } else {
                    current.push(child.get());
                }
            }

            if (!current.content)
                throw std::runtime_error("Internal expression noun issue occurred.");

            results.emplace_back(applyUnary(current, unary));
        }

        postfix = pick<Ternary, As>(true);

        std::vector<utils::BinaryOperation> operatorOrder = { utils::BinaryOperation::Mul, utils::BinaryOperation::Div,
            utils::BinaryOperation::Add, utils::BinaryOperation::Sub, utils::BinaryOperation::Mod,

            utils::BinaryOperation::Equals, utils::BinaryOperation::NotEquals, utils::BinaryOperation::Greater,
            utils::BinaryOperation::GreaterEqual, utils::BinaryOperation::Lesser, utils::BinaryOperation::LesserEqual,

            utils::BinaryOperation::And, utils::BinaryOperation::Or };

        while (!operators.empty()) {
            for (auto order : operatorOrder) {
                for (int64_t i = 0; i < operators.size(); i++) {
                    Operator *op = operators[i];

                    if (op->op != order)
                        continue;

                    // Move more!!!
                    utils::ExpressionResult a = std::move(results[i]);
                    utils::ExpressionResult b = std::move(results[i + 1]);

                    results.erase(results.begin() + i, results.begin() + i + 2);
                    operators.erase(operators.begin() + i);

                    results.insert(results.begin() + i,
                        utils::ExpressionCombinator(std::make_unique<utils::ExpressionResult>(std::move(a)),
                            std::make_unique<utils::ExpressionResult>(std::move(b)), op));
                }
            }
        }

        if (results.size() != 1)
            throw std::runtime_error("Internal result picker issue occurred.");

        result = std::move(results.front());

        if (postfix) {
            result = utils::ExpressionOperation(
                std::make_unique<utils::ExpressionResult>(std::move(result)), postfix.get());
        }
    }
}
