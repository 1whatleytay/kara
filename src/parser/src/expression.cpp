#include <parser/expression.h>

#include <parser/literals.h>
#include <parser/operator.h>

#include <array>
#include <stdexcept>
#include <unordered_set>

namespace kara::parser {
    utils::ExpressionResult applyUnary(
        const utils::ExpressionNoun &current, const std::vector<const parser::Unary *> &unary) {
        utils::ExpressionResult value = current;

        for (auto a = unary.rbegin(); a < unary.rend(); a++)
            value = utils::ExpressionOperation(std::make_unique<utils::ExpressionResult>(std::move(value)), *a);

        return value;
    }

    utils::ExpressionResult combine(
        std::vector<utils::ExpressionResult> results,
        std::vector<const Operator *> operators) {
        // to support /, we need a list of operators that have the left-grouping behavior

        // okay, so first we go through the array and look for group-to-left operators
        // if we find one we

        assert(operators.size() == results.size() - 1);

        std::array operatorOrder = {
            utils::BinaryOperation::Fallback,

            utils::BinaryOperation::Mul,
            utils::BinaryOperation::Div,
            utils::BinaryOperation::Add,
            utils::BinaryOperation::Sub,
            utils::BinaryOperation::Mod,

            utils::BinaryOperation::Equals,
            utils::BinaryOperation::NotEquals,
            utils::BinaryOperation::Greater,
            utils::BinaryOperation::GreaterEqual,
            utils::BinaryOperation::Lesser,
            utils::BinaryOperation::LesserEqual,

            utils::BinaryOperation::And,
            utils::BinaryOperation::Or,
        };

        while (!operators.empty()) {
            for (auto order : operatorOrder) {
                for (int64_t i = 0; i < operators.size(); i++) {
                    const Operator *op = operators[i];

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

        return std::move(results.front());
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
                // Ternary, As, Slash here?
                if (!push<Call, Index, Dot, Operator, Ternary, As, Slash>(true)) {
                    exit = true;
                    break;
                }

                if (children.back()->is(Kind::Operator))
                    break;
            }
        }

        // Calculate result (operator precedence).
        std::vector<utils::ExpressionResult> results;
        std::vector<const Operator *> operators;

        std::unordered_set<parser::Kind> groupsToLeft = {
            parser::Kind::As,
            parser::Kind::Ternary,
            parser::Kind::Slash,
        };

        {
            std::vector<const Unary *> unary;
            utils::ExpressionNoun current;

            auto commit = [&]() {
                if (current.content) {
                    results.emplace_back(applyUnary(current, unary));

                    unary.clear();
                    current = {};
                }
            };

            for (const auto &child : children) {
                if (groupsToLeft.find(child->is<parser::Kind>()) != groupsToLeft.end()) {
                    // push unary/current
                    commit();

                    // pass results/operators to combine
                    auto combination = combine(std::move(results), std::move(operators));
                    auto operation = utils::ExpressionOperation(
                        std::make_unique<utils::ExpressionResult>(std::move(combination)), child.get());

                    results.clear();

                    results.emplace_back(std::move(operation));

                    // implied
                    // results.clear();
                    // operators.clear();
                } else if (child->is(Kind::Operator)) {
                    operators.push_back(child->as<Operator>());

                    commit();
                } else if (child->is(Kind::Unary)) {
                    unary.push_back(child->as<Unary>());
                } else {
                    current.push(child.get());
                }
            }

            commit();

            result = combine(std::move(results), std::move(operators));
        }
    }
}
