#include <builder/builder.h>

#include <builder/search.h>

#include <parser/scope.h>
#include <parser/debug.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>
#include <parser/reference.h>

#include <fmt/printf.h>

std::optional<BuilderVariableInfo> BuilderScope::findVariable(const VariableNode *node) const {
    const BuilderScope *scope = this;

    // yeah double pointer
    // i'm just worried that a BuilderResult might outlive a BuilderVariable for some reason
    const std::shared_ptr<MultipleLifetime> *lifetime = nullptr;

    while (scope) {
        if (!lifetime) {
            auto newLifetime = scope->lifetimes.find(node);

            if (newLifetime != scope->lifetimes.end()) {
                lifetime = &newLifetime->second; // god i hope this is a proper reference
            }
        }

        auto newVariable = scope->variables.find(node);

        if (newVariable != scope->variables.end()) {
            if (!lifetime)
                throw std::runtime_error("Missing lifetime.");

            if (!*lifetime)
                throw std::runtime_error("Incorrect lifetime for variable.");

            return BuilderVariableInfo { *newVariable->second, *lifetime };
        }

        scope = scope->parent;
    }

    return std::nullopt;
}

Value *BuilderScope::get(const BuilderResult &result) {
    return result.kind == BuilderResult::Kind::Reference ? current.CreateLoad(result.value) : result.value;
}

void BuilderScope::build(const CodeNode *node) {
    for (const auto &child : node->children) {
        switch (child->is<Kind>()) {
            case Kind::Variable:
                variables[child->as<VariableNode>()] =
                    std::make_unique<BuilderVariable>(child->as<VariableNode>(), *this);

                break;

            case Kind::Assign:
                makeAssign(child->as<AssignNode>());
                break;

            case Kind::Statement:
                makeStatement(child->as<StatementNode>());
                break;

            case Kind::Block: {
                BuilderScope sub(child->children.front()->as<CodeNode>(), *this);

                current.CreateBr(sub.openingBlock);

                currentBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
                current.SetInsertPoint(currentBlock);

                sub.current.CreateBr(currentBlock);

                break;
            }

            case Kind::Expression:
                makeExpression(child->as<ExpressionNode>()->result);
                break;

            case Kind::Debug: {
                auto *e = child->as<DebugNode>();

                LineDetails details(e->getState().text, e->index, false);

                switch (e->type) {
                    case DebugNode::Type::Expression: {
                        BuilderResult result = makeExpression(e->children.front()->as<ExpressionNode>()->result);

                        std::vector<MultipleLifetime *> debugLifetimes =
                            expand(*result.lifetime, result.lifetimeDepth);
                        std::vector<std::string> concat(debugLifetimes.size());

                        std::transform(debugLifetimes.begin(), debugLifetimes.end(), concat.begin(),
                            [](MultipleLifetime *x) { return toString(*x); });

                        fmt::print("[DEBUG:{}] [ {} ] (depth: {})\n",
                            details.lineNumber, fmt::join(concat, " "), result.lifetimeDepth);

                        break;
                    }

                        // this switch is really hard to read
                    case DebugNode::Type::Reference: {
                        auto *reference = e->children.front()->as<ReferenceNode>();

                        const auto *astVar = search::exclusive::scope(child.get(), [reference](const Node *node) {
                            return node->is(Kind::Variable)
                                && node->as<VariableNode>()->name == reference->name;
                        })->as<VariableNode>();

                        if (astVar) {
                            auto varInfo = findVariable(astVar);

                            if (varInfo) {
                                fmt::print("[DEBUG:{}] {} (scope: {})\n", details.lineNumber,
                                    toString(*varInfo.value().lifetime), varInfo.value().variable.lifetimeLevel);
                            } else {
                                fmt::print("[DEBUG:{}] No lifetime for {}\n", details.lineNumber, astVar->name);
                            }
                        } else {
                            fmt::print("[DEBUG:{}] Cannot find {}\n", details.lineNumber, reference->name);
                        }

                        break;
                    }
                }

                break;
            }

            default:
                assert(false);
        }
    }
}

std::vector<MultipleLifetime *> BuilderScope::expand(MultipleLifetime &lifetime, int32_t depth, bool doCopy) {
    std::vector<MultipleLifetime *> result = { &lifetime };

    assert(depth >= 0); // i made it an int so i can detect these problems

    for (int32_t a = 0; a < depth; a++) {
        std::vector<MultipleLifetime *> newLifetimes;

        for (MultipleLifetime *b : result) {
            for (const auto &sub : *b) {
                switch (sub->kind) {
                    case Lifetime::Kind::Reference:
                        newLifetimes.push_back(dynamic_cast<ReferenceLifetime *>(sub.get())->children.get());
                        break;

                    case Lifetime::Kind::Variable: {
                        const VariableNode *node = dynamic_cast<VariableLifetime *>(sub.get())->node;
                        // I hate so much of this system...
                        MultipleLifetime *p;

                        auto inScope = lifetimes.find(node);
                        if (inScope == lifetimes.end()) {
                            auto x = findVariable(node);

                            assert(x.has_value());

                            if (doCopy) {
                                std::shared_ptr<MultipleLifetime> temp = copy(*x.value().lifetime);

                                p = temp.get();
                                lifetimes[node] = std::move(temp);
                            } else {
                                p = x.value().lifetime.get();
                            }
                        } else {
                            p = inScope->second.get();
                        }

                        auto y = expand(*p, 1);
                        newLifetimes.insert(newLifetimes.end(), y.begin(), y.end());

                        break;
                    }

                    default:
                        assert(false);
                }
            }
        }

        result = std::move(newLifetimes);
    }

    return result;
}

BuilderScope::BuilderScope(const CodeNode *node, BuilderScope &parent)
    :parent(&parent), function(parent.function), current(function.builder.context) {
    lifetimeLevel = parent.lifetimeLevel + 1;

    // same code as other constructor, optimize out?
    openingBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
    currentBlock = openingBlock;

    current.SetInsertPoint(currentBlock);

    build(node);
}

BuilderScope::BuilderScope(const CodeNode *node, BuilderFunction &function)
    : function(function), current(function.builder.context) {
    openingBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
    currentBlock = openingBlock;

    current.SetInsertPoint(currentBlock);

    const FunctionNode *astFunction = function.node;
    const Function *llvmFunction = function.function;

    // Create parameters within scope.
    for (size_t a = 0; a < astFunction->parameterCount; a++) {
        const auto *parameterNode = astFunction->children[a]->as<VariableNode>();

        variables[parameterNode] = std::make_unique<BuilderVariable>(
            parameterNode, llvmFunction->getArg(a), *this);
    }

    build(node);
}
