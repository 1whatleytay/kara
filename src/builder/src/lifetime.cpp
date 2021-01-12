#include <builder/lifetime.h>

#include <builder/builder.h>

#include <parser/variable.h>

#include <fmt/format.h>

bool Lifetime::operator==(const Lifetime &lifetime) const {
    return kind == lifetime.kind
        && placeholderVariable == lifetime.placeholderVariable
        && placeholderUnique == lifetime.placeholderUnique;
}

bool Lifetime::operator!=(const Lifetime &lifetime) const {
    return !operator==(lifetime);
}

std::string Lifetime::placeholderString() const {
    return placeholderVariable ? fmt::format("({}.{})", placeholderVariable->name, placeholderUnique) : "";
}

Lifetime::Lifetime(Kind kind) : kind(kind) { }
Lifetime::Lifetime(Kind kind, const VariableNode *placeholder, uint32_t unique)
    : kind(kind), placeholderVariable(placeholder), placeholderUnique(unique) { }

std::string VariableLifetime::toString() const {
    return fmt::format("{}{}", placeholderString(), node ? node->name : "<anon>");
}

std::shared_ptr<Lifetime> VariableLifetime::copy() const {
    return std::make_shared<VariableLifetime>(node, placeholderVariable, placeholderUnique);
}

bool VariableLifetime::resolves(const BuilderScope &scope) const {
    // Is a placeholder node, definitely outlives the function... I think.
    if (!node)
        return true;

    return scope.findVariable(node).has_value();
}

bool VariableLifetime::operator==(const Lifetime &lifetime) const {
    if (!Lifetime::operator==(lifetime))
        return false;

    auto varLifetime = dynamic_cast<const VariableLifetime &>(lifetime);

    return node == varLifetime.node;
}

VariableLifetime::VariableLifetime(const VariableNode *node, const VariableNode *placeholder, uint32_t unique)
    : Lifetime(Lifetime::Kind::Variable, placeholder, unique), node(node) { }

std::string ReferenceLifetime::toString() const {
    return fmt::format("&{}{}", placeholderString(), children->toString());
}

std::shared_ptr<Lifetime> ReferenceLifetime::copy() const {
    return std::make_shared<ReferenceLifetime>(
        std::make_shared<MultipleLifetime>(children->copy()),
        placeholderVariable, placeholderUnique);
}

bool ReferenceLifetime::resolves(const BuilderScope &scope) const {
    return children->resolves(scope);
}

bool ReferenceLifetime::operator==(const Lifetime &lifetime) const {
    if (!Lifetime::operator==(lifetime))
        return false;

    auto refLifetime = dynamic_cast<const ReferenceLifetime &>(lifetime);

    return children->compare(*refLifetime.children);
}

ReferenceLifetime::ReferenceLifetime(std::shared_ptr<MultipleLifetime> lifetime,
    const VariableNode *representing, uint32_t unique)
    : Lifetime(Lifetime::Kind::Reference, representing, unique), children(std::move(lifetime)) { }
ReferenceLifetime::ReferenceLifetime(const ReferenceTypename &type, const VariableNode *representing, uint32_t unique)
    : Lifetime(Lifetime::Kind::Reference, representing, unique) {
    Typename &subType = *type.value;

    children = std::make_shared<MultipleLifetime>();
    children->push_back(makeAnonymousLifetime(subType, representing, unique + 1));
}

std::shared_ptr<Lifetime> makeDefaultLifetime(
    const Typename &type, const VariableNode *representing, uint32_t unique) {
    struct {
        const VariableNode *representing;
        uint32_t unique;

        std::shared_ptr<Lifetime> operator()(const ReferenceTypename &type) const {
            return std::make_shared<ReferenceLifetime>(
                std::make_shared<MultipleLifetime>(), representing, unique);
        }

        std::shared_ptr<Lifetime> operator()(const StackTypename &) const {
            return nullptr;
        }

        std::shared_ptr<Lifetime> operator()(const FunctionTypename &) const {
            assert(false);
        }
    } visitor { representing, unique };

    return std::visit(visitor, type);
}

std::shared_ptr<Lifetime> makeAnonymousLifetime(
    const Typename &type, const VariableNode *representing, uint32_t unique) {
    struct {
        const VariableNode *representing;
        uint32_t unique;

        std::shared_ptr<Lifetime> operator()(const ReferenceTypename &type) const {
            return std::make_shared<ReferenceLifetime>(type, representing, unique);
        }

        std::shared_ptr<Lifetime> operator()(const StackTypename &) const {
            return std::make_shared<VariableLifetime>(nullptr, representing, unique);
        }

        std::shared_ptr<Lifetime> operator()(const FunctionTypename &) const {
            assert(false);
        }
    } visitor { representing, unique };

    return std::visit(visitor, type);
}

std::string MultipleLifetime::toString() const {
    std::vector<std::string> children(size());
    std::transform(begin(), end(), children.begin(), [](const auto &e) {
        return e->toString();
    });

    return fmt::format("{{ {} }}", fmt::join(children, ", "));
}

MultipleLifetime MultipleLifetime::copy() const {
    MultipleLifetime result(size());
    std::transform(begin(), end(), result.begin(), [](const auto &x) { return x->copy(); });

    return result;
}

bool MultipleLifetime::compare(const MultipleLifetime &other) const {
    if (size() != other.size())
        return false;

    for (size_t a = 0; a < size(); a++) {
        if (operator[](a) != other[a]) {
            return false;
        }
    }

    return true;
}

bool MultipleLifetime::resolves(const BuilderScope &scope) const {
    return std::all_of(begin(), end(),
        [scope](const std::shared_ptr<Lifetime> &l) { return l->resolves(scope); });
}

MultipleLifetime::MultipleLifetime(size_t size) : std::vector<std::shared_ptr<Lifetime>>(size) { }
