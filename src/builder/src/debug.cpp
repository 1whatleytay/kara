#include <builder/builder.h>

#include <builder/search.h>

#include <parser/debug.h>
#include <parser/variable.h>
#include <parser/reference.h>

#include <fmt/printf.h>

void BuilderScope::makeDebug(const DebugNode *node) {
    LineDetails details(node->getState().text, node->index, false);

    switch (node->type) {
        case DebugNode::Type::Expression: {
            BuilderResult result = makeExpression(node->children.front()->as<ExpressionNode>()->result);

            std::vector<MultipleLifetime *> debugLifetimes =
                expand({ result.lifetime.get() }, result.lifetimeDepth);
            std::vector<std::string> concat(debugLifetimes.size());

            std::transform(debugLifetimes.begin(), debugLifetimes.end(), concat.begin(),
                [](MultipleLifetime *x) { return x->toString(); });

            fmt::print("[DEBUG:{}] [ {} ] (depth: {})\n",
                details.lineNumber, fmt::join(concat, " "), result.lifetimeDepth);

            break;
        }

        // this switch is really hard to read
        case DebugNode::Type::Reference: {
            auto *reference = node->children.front()->as<ReferenceNode>();

            const auto *astVar = search::exclusive::scope(node, [reference](const Node *node) {
                return node->is(Kind::Variable)
                    && node->as<VariableNode>()->name == reference->name;
            })->as<VariableNode>();

            if (astVar) {
                auto varInfo = findVariable(astVar);

                varInfo.value().lifetime->simplify();

                if (varInfo) {
                    fmt::print("[DEBUG:{}] {}\n", details.lineNumber, varInfo.value().lifetime->toString());
                } else {
                    fmt::print("[DEBUG:{}] No lifetime for {}\n", details.lineNumber, astVar->name);
                }
            } else {
                fmt::print("[DEBUG:{}] Cannot find {}\n", details.lineNumber, reference->name);
            }

            break;
        }

        case DebugNode::Type::Return: {
            MultipleLifetime *final = function.type.returnTransformFinal.get();

            if (final) {
                fmt::print("[DEBUG:{}] {}\n", details.lineNumber, final->toString());
            } else {
                fmt::print("[DEBUG:{}] No return transform lifetime.\n", details.lineNumber);
            }

            break;
        }

        case DebugNode::Type::Type: {
            auto *reference = node->children.front()->as<ReferenceNode>();

            const auto *astVar = search::exclusive::scope(node, [reference](const Node *node) {
                return node->is(Kind::Variable)
                    && node->as<VariableNode>()->name == reference->name;
            })->as<VariableNode>();

            if (astVar) {
                auto varInfo = findVariable(astVar);

                if (varInfo) {
                    fmt::print("[DEBUG:{}] {}\n", details.lineNumber, toString(varInfo.value().variable.type));
                } else {
                    fmt::print("[DEBUG:{}] No lifetime for {}\n", details.lineNumber, astVar->name);
                }
            } else {
                fmt::print("[DEBUG:{}] Cannot find {}\n", details.lineNumber, reference->name);
            }
        }
    }
}