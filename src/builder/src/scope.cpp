#include <builder/builder.h>

#include <builder/search.h>

#include <parser/if.h>
#include <parser/for.h>
#include <parser/block.h>
#include <parser/debug.h>
#include <parser/scope.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>
#include <parser/reference.h>

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

std::vector<MultipleLifetime *> BuilderScope::expand(const std::vector<MultipleLifetime *> &lifetime, bool doCopy) {
    std::vector<MultipleLifetime *> newLifetimes;

    for (MultipleLifetime *b : lifetime) {
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
                            auto temp = std::make_shared<MultipleLifetime>(x.value().lifetime->copy());

                            p = temp.get();
                            lifetimes[node] = std::move(temp);
                        } else {
                            p = x.value().lifetime.get();
                        }
                    } else {
                        p = inScope->second.get();
                    }

                    auto y = expand({ p }, doCopy);
                    newLifetimes.insert(newLifetimes.end(), y.begin(), y.end());

                    break;
                }

                default:
                    assert(false);
            }
        }
    }

    return newLifetimes;
}

std::vector<MultipleLifetime *> BuilderScope::expand(
    std::vector<MultipleLifetime *> result, int32_t depth, bool doCopy) {
    assert(depth >= 0); // i made it an int so i can detect these problems

    for (int32_t a = 0; a < depth; a++)
        result = expand(result, doCopy);

    return result;
}

void BuilderScope::join(LifetimeMatches &matches,
    std::vector<MultipleLifetime *> lifetime, const MultipleLifetime &initial) {
//    std::vector<MultipleLifetime *> temp = lifetime;

    const MultipleLifetime *currentLifetime = &initial;

    while (currentLifetime) {
        assert(currentLifetime->size() == 1);

        Lifetime *mainPlaceholder = currentLifetime->front().get();

        assert(mainPlaceholder->id.first);

        // Oh dear this name
        MultipleLifetime currentLifetimeLifetime;

        for (MultipleLifetime *x : lifetime) {
            auto temp = x->copy();
            currentLifetimeLifetime.insert(currentLifetimeLifetime.end(), temp.begin(), temp.end());
        }

        matches[mainPlaceholder->id] = std::move(currentLifetimeLifetime);

        lifetime = expand(lifetime, 1);

        if (mainPlaceholder->kind == Lifetime::Kind::Reference) {
            currentLifetime = dynamic_cast<ReferenceLifetime *>(mainPlaceholder)->children.get();
        } else {
            currentLifetime = nullptr;
        };
    }
}

void BuilderScope::build(const LifetimeMatches &matches,
    const std::vector<MultipleLifetime *> &lifetime, const MultipleLifetime &final) {
    MultipleLifetime finalResult = final;
    std::vector<MultipleLifetime *> lifetimeResult = lifetime;

    while (!finalResult.empty() && !lifetimeResult.empty()) {
        for (MultipleLifetime *l : lifetimeResult) {
            l->clear();

            for (const auto &x : finalResult) {
                assert(x->id.first);

                auto match = matches.find(x->id);
                assert(match != matches.end());

                auto temp = match->second.copy(); // pain, remove later if you think good things will happen

                l->insert(l->end(), temp.begin(), temp.end());
            }

            l->simplify();
        }

        auto expandable = [](const std::shared_ptr<Lifetime> &l) {
            return l->kind != Lifetime::Kind::Variable || dynamic_cast<VariableLifetime &>(*l).node;
        };

        if (!std::all_of(finalResult.begin(), finalResult.end(), expandable))
            break;

        finalResult = flatten(expand({ &finalResult }));
        lifetimeResult = expand(lifetimeResult);
    }
}

void BuilderScope::mergeLifetimes(const BuilderScope &sub) {
    for (const auto &pair : sub.lifetimes) {
        // Don't merge any variables that do not exist beyond that scope.
        if (sub.variables.find(pair.first) != sub.variables.end())
            continue;

        std::shared_ptr<MultipleLifetime> &lifetime = lifetimes[pair.first];

        // Going to try to reuse it actually... other scope is going to be destructed anyway.
        lifetime = pair.second;
        lifetime->simplify();
    }
}

void BuilderScope::mergePossibleLifetimes(const BuilderScope &sub) {
    for (auto &pair : sub.lifetimes) {
        // Don't merge any variables that do not exist beyond that scope.
        if (sub.variables.find(pair.first) != sub.variables.end())
            continue;

        MultipleLifetime &lifetime = *lifetimes[pair.first];

        // Add to existing lifetimes.
        lifetime.insert(lifetime.begin(), pair.second->begin(), pair.second->end());
        lifetime.simplify();
    }
}

BuilderScope::BuilderScope(const CodeNode *node, BuilderFunction &function, BuilderScope *parent)
    : parent(parent), function(function), current(function.builder.context) {
    openingBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
    currentBlock = openingBlock;

    current.SetInsertPoint(currentBlock);

    if (!parent) {
        const FunctionNode *astFunction = function.node;
        const Function *llvmFunction = function.function;

        // Create parameters within scope.
        for (size_t a = 0; a < astFunction->parameterCount; a++) {
            const auto *parameterNode = astFunction->children[a]->as<VariableNode>();

            Argument *argument = llvmFunction->getArg(a);
            argument->setName(parameterNode->name);

            variables[parameterNode] = std::make_shared<BuilderVariable>(parameterNode, argument, *this);
        }
    }

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

            case Kind::Block:
                makeBlock(child->as<BlockNode>());
                break;

            case Kind::If:
                makeIf(child->as<IfNode>());
                break;

            case Kind::For:
                makeFor(child->as<ForNode>());
                break;

            case Kind::Expression:
                makeExpression(child->as<ExpressionNode>()->result);
                break;

            case Kind::Debug:
                makeDebug(child->as<DebugNode>());
                break;

            default:
                assert(false);
        }
    }
}

BuilderScope::BuilderScope(const CodeNode *node, BuilderScope &parent)
    : BuilderScope(node, parent.function, &parent) { }
BuilderScope::BuilderScope(const CodeNode *node, BuilderFunction &function)
    : BuilderScope(node, function, nullptr) { }
