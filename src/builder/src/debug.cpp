#include <builder/builder.h>

#include <parser/search.h>

#include <parser/debug.h>
#include <parser/variable.h>
#include <parser/reference.h>

#include <fmt/printf.h>

void BuilderScope::makeDebug(const DebugNode *node) {
    LineDetails details(node->getState().text, node->index, false);

    switch (node->type) {
        case DebugNode::Type::Type: {
            auto *reference = node->children.front()->as<ReferenceNode>();

            const auto *astVar = search::exclusive::scope(node, [reference](const Node *node) {
                return node->is(Kind::Variable)
                    && node->as<VariableNode>()->name == reference->name;
            })->as<VariableNode>();

            if (astVar) {
                auto varInfo = findVariable(astVar);

                if (varInfo) {
                    fmt::print("[DEBUG:{}] {}\n", details.lineNumber, toString(varInfo->type));
                } else {
                    fmt::print("[DEBUG:{}] No lifetime for {}\n", details.lineNumber, astVar->name);
                }
            } else {
                fmt::print("[DEBUG:{}] Cannot find {}\n", details.lineNumber, reference->name);
            }
        }
    }
}