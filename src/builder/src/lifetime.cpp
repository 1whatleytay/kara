#include <builder/lifetime.h>

#include <builder/builder.h>

#include <parser/variable.h>

#include <fmt/format.h>

Lifetime::Lifetime(Kind kind) : kind(kind) { }
Lifetime::Lifetime(Kind kind, const VariableNode *placeholder, uint32_t unique)
    : kind(kind), placeholderVariable(placeholder), placeholderUnique(unique) { }

VariableLifetime::VariableLifetime(const VariableNode *node, const VariableNode *placeholder, uint32_t unique)
    : Lifetime(Lifetime::Kind::Variable, placeholder, unique), node(node) { }

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

std::string Lifetime::placeholderString() const {
    return placeholderVariable ? fmt::format("({}.{})", placeholderVariable->name, placeholderUnique) : "";
}

std::string VariableLifetime::toString() const {
    return fmt::format("{}{}", placeholderString(), node ? node->name : "<anon>");
}

std::shared_ptr<Lifetime> VariableLifetime::copy() const {
    return std::make_shared<VariableLifetime>(node, placeholderVariable, placeholderUnique);
}

int64_t VariableLifetime::lifetimeLevel(const BuilderScope &scope) const {
    auto var = scope.findVariable(node);

    assert(var.has_value());

    return var.value().variable.lifetimeLevel;
}

std::string ReferenceLifetime::toString() const {
    return fmt::format("&{}{}", placeholderString(), ::toString(*children));
}

std::shared_ptr<Lifetime> ReferenceLifetime::copy() const {
    return std::make_shared<ReferenceLifetime>(::copy(*children), placeholderVariable, placeholderUnique);
}

int64_t ReferenceLifetime::lifetimeLevel(const BuilderScope &scope) const {
    return ::lifetimeLevel(*children, scope);
}

std::string toString(const MultipleLifetime &lifetime) {
    std::vector<std::string> children(lifetime.size());
    std::transform(lifetime.begin(), lifetime.end(), children.begin(), [](const auto &e) {
        return e->toString();
    });

    return fmt::format("{{ {} }}", fmt::join(children, ", "));
}

std::shared_ptr<MultipleLifetime> copy(const MultipleLifetime &lifetime) {
    std::shared_ptr<MultipleLifetime> result = std::make_shared<MultipleLifetime>(lifetime.size());
    std::transform(lifetime.begin(), lifetime.end(), result->begin(), [](const auto &x) { return x->copy(); });

    return result;
}

int64_t lifetimeLevel(const MultipleLifetime &lifetime, const BuilderScope &scope) {
    int64_t level = -1;

    for (const auto &e : lifetime) {
        int64_t eLevel = e->lifetimeLevel(scope);

        if (eLevel > level)
            level = eLevel;
    }

    return level;
}
