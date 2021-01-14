#include <parser/expression.h>

#include <parser/call.h>
#include <parser/bool.h>
#include <parser/number.h>
#include <parser/reference.h>
#include <parser/parentheses.h>

void ExpressionNoun::push(const Node *node) {
    if (content)
        modifiers.push_back(node);
    else
        content = node;
}

ExpressionOperation::ExpressionOperation(std::unique_ptr<ExpressionResult> a, UnaryNode *op)
    : a(std::move(a)), op(op) { }

ExpressionCombinator::ExpressionCombinator(
    std::unique_ptr<ExpressionResult> a, std::unique_ptr<ExpressionResult> b, OperatorNode *op)
    : a(std::move(a)), b(std::move(b)), op(op) { }

static ExpressionResult applyUnary(const ExpressionNoun &current, const std::vector<UnaryNode *> &unary) {
    ExpressionResult value = current;

    for (auto a = unary.rbegin(); a < unary.rend(); a++)
        value = ExpressionOperation(std::make_unique<ExpressionResult>(std::move(value)), *a);

    return value;
}

ExpressionNode::ExpressionNode(Node *parent) : Node(parent, Kind::Expression) {
    bool exit = false;

    while(!end() && !exit) {
        while (push<UnaryNode>(true));

        push<ParenthesesNode, BoolNode, NumberNode, ReferenceNode>();

        while (true) {
            if (!push<CallNode, OperatorNode>(true)) {
                exit = true;
                break;
            }

            if (children.back()->is(Kind::Operator))
                break;
        }
    }

    // Calculate result (operator precedence).
    std::vector<ExpressionResult> results;
    std::vector<OperatorNode *> operators;

    {
        std::vector<UnaryNode *> unary;
        ExpressionNoun current;

        for (const auto &child : children) {
            if (child->is(Kind::Operator)) {
                operators.push_back(child->as<OperatorNode>());

                results.emplace_back(applyUnary(current, unary));

                unary.clear();
                current = { };
            } else if (child->is(Kind::Unary)) {
                unary.push_back(child->as<UnaryNode>());
            } else {
                current.push(child.get());
            }
        }

        if (!current.content)
            throw std::runtime_error("Internal expression noun issue occurred.");

        results.emplace_back(applyUnary(current, unary));
    }

    std::vector<OperatorNode::Operation> operatorOrder = {
        OperatorNode::Operation::Mul,
        OperatorNode::Operation::Div,
        OperatorNode::Operation::Add,
        OperatorNode::Operation::Sub,

        OperatorNode::Operation::Equals,
        OperatorNode::Operation::NotEquals,
        OperatorNode::Operation::Greater,
        OperatorNode::Operation::GreaterEqual,
        OperatorNode::Operation::Lesser,
        OperatorNode::Operation::LesserEqual,

        OperatorNode::Operation::And,
        OperatorNode::Operation::Or
    };

    while (!operators.empty()) {
        for (auto order : operatorOrder) {
            for (size_t i = 0; i < operators.size(); i++) {
                OperatorNode *op = operators[i];

                if (op->op != order)
                    continue;

                // Move more!!!
                ExpressionResult a = std::move(results[i]);
                ExpressionResult b = std::move(results[i + 1]);

                results.erase(results.begin() + i, results.begin() + i + 2);
                operators.erase(operators.begin() + i);

                results.insert(results.begin() + i,
                    ExpressionCombinator(
                        std::make_unique<ExpressionResult>(std::move(a)),
                        std::make_unique<ExpressionResult>(std::move(b)), op));
            }
        }
    }

    if (results.size() != 1)
        throw std::runtime_error("Internal result picker issue occurred.");

    result = std::move(results.front());
}
