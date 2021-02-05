#include <builder/builder.h>

#include <builder/lifetime/lifetime.h>
#include <builder/lifetime/multiple.h>
#include <builder/lifetime/reference.h>
#include <builder/lifetime/variable.h>
#include <builder/lifetime/array.h>

std::vector<MultipleLifetime *> BuilderScope::expand(const std::vector<MultipleLifetime *> &lifetime, bool doCopy) {
    std::vector<MultipleLifetime *> newLifetimes;

    for (MultipleLifetime *b : lifetime) {
        for (const auto &sub : *b) {
            switch (sub->kind) {
                case Lifetime::Kind::Reference:
                    newLifetimes.push_back(dynamic_cast<ReferenceLifetime &>(*sub).children.get());
                    break;

                case Lifetime::Kind::Array:
                    newLifetimes.push_back(dynamic_cast<ArrayLifetime &>(*sub).take().get());
                    break;

                case Lifetime::Kind::Variable: {
                    const VariableNode *node = dynamic_cast<VariableLifetime &>(*sub).node;
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

                case Lifetime::Kind::Null:
                    break; // resolves will catch it im sure

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

        currentLifetime = mainPlaceholder->kind == Lifetime::Kind::Reference
            ? dynamic_cast<ReferenceLifetime *>(mainPlaceholder)->children.get() : nullptr;
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
                // i wish i had better system
                if (!x->id.first) {
                    if (x->kind == Lifetime::Kind::Variable && !dynamic_cast<VariableLifetime &>(*x).node) {
                        continue; // yikes
                    } else {
                        // if it contains a node bad things will happen
                        l->insert(l->end(), x->copy());
                    }
                } else {
                    auto match = matches.find(x->id);
                    assert(match != matches.end());

                    auto temp = match->second.copy(); // pain, remove later if you think good things will happen

                    l->insert(l->end(), temp.begin(), temp.end());
                }
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
