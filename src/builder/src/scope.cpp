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

//    fmt::print("Input:\n");
//    fmt::print("\tInitial: {}\n", toString(initial));
//    std::vector<std::string> strings(temp.size());
//    std::transform(temp.begin(), temp.end(), strings.begin(),
//        [](MultipleLifetime *l) { return toString(*l); });
//    fmt::print("\tExpression: [ {} ]\n", fmt::join(strings, ", "));
//
//    fmt::print("Output:\n");
//    for (const auto &e : matches) {
//        fmt::print("\t({}:{}) {}\n", e.first.first->name, e.first.second, toString(*e.second));
//    }
}

void BuilderScope::build(const LifetimeMatches &matches,
    const std::vector<MultipleLifetime *> &lifetime, const MultipleLifetime &final) {
//    for (const auto &e : matches) {
//        fmt::print("Match: ({}:{}) {}\n", e.first.first->name, e.first.second, e.second.toString());
//    }
//
//    {
//        std::vector<std::string> strings(lifetime.size());
//        std::transform(lifetime.begin(), lifetime.end(), strings.begin(),
//            [](MultipleLifetime *l) { return l->toString(); });
//        fmt::print(" From: [ {} ]\n", fmt::join(strings, ", "));
//    }

//    fmt::print("Final: {}\n", final.toString());

    MultipleLifetime finalResult = final;
    std::vector<MultipleLifetime *> lifetimeResult = lifetime;

    while (!finalResult.empty() && !lifetimeResult.empty()) {
//        fmt::print("Iter Final: {}\n", finalResult.toString());
//
//        {
//            std::vector<std::string> strings(lifetimeResult.size());
//            std::transform(lifetimeResult.begin(), lifetimeResult.end(), strings.begin(),
//                [](MultipleLifetime *l) { return l->toString(); });
//            fmt::print("Iter Life:  [ {} ]\n", fmt::join(strings, ", "));
//        }

        for (MultipleLifetime *l : lifetimeResult) {
            l->clear();

            for (const auto &x : finalResult) {
                assert(x->id.first);

                auto match = matches.find(x->id);
                assert(match != matches.end());

//                fmt::print("Adding {} to {}\n", match->second.toString(), l->toString());

                auto temp = match->second.copy(); // pain, remove later if you think good things will happen

                l->insert(l->end(), temp.begin(), temp.end());
            }

//            fmt::print("L Result: {}\n", l->toString());
        }

        auto expandable = [](const std::shared_ptr<Lifetime> &l) {
            return l->kind != Lifetime::Kind::Variable || dynamic_cast<VariableLifetime &>(*l).node;
        };

        if (!std::all_of(finalResult.begin(), finalResult.end(), expandable))
            break;

        finalResult = flatten(expand({ &finalResult }));
        lifetimeResult = expand(lifetimeResult);
    }

//    {
//        std::vector<std::string> strings(lifetime.size());
//        std::transform(lifetime.begin(), lifetime.end(), strings.begin(),
//            [](MultipleLifetime *l) { return l->toString(); });
//        fmt::print(" To: [ {} ]\n", fmt::join(strings, ", "));
//    }
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

            variables[parameterNode] = std::make_unique<BuilderVariable>(
                parameterNode, llvmFunction->getArg(a), *this);
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

            case Kind::Block: {
                BuilderScope sub(child->children.front()->as<CodeNode>(), *this);

                current.CreateBr(sub.openingBlock);

                currentBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
                current.SetInsertPoint(currentBlock);

                sub.current.CreateBr(currentBlock);

                // Merge lifetimes
                for (auto &pair : sub.lifetimes) {
                    // Don't merge any variables that do not exist beyond that scope.
                    if (sub.variables.find(pair.first) != sub.variables.end())
                        continue;

                    // Going to try to reuse it actually... other scope is going to be destructed anyway.
                    lifetimes[pair.first] = std::move(pair.second);
                }

                break;
            }

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
