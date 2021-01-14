#include <builder/lifetime.h>

#include <builder/builder.h>

#include <parser/variable.h>

#include <fmt/format.h>

bool Lifetime::operator==(const Lifetime &lifetime) const {
    return kind == lifetime.kind && id == lifetime.id;
}

bool Lifetime::operator!=(const Lifetime &lifetime) const {
    return !operator==(lifetime);
}

std::string Lifetime::placeholderString() const {
    return id.first ? fmt::format("({}.{})", id.first->name, id.second) : "";
}

Lifetime::Lifetime(Kind kind) : kind(kind) { }
Lifetime::Lifetime(Kind kind, PlaceholderId id) : kind(kind), id(std::move(id)) { }

std::string VariableLifetime::toString() const {
    return fmt::format("{}{}", placeholderString(), node ? node->name : "<anon>");
}

std::shared_ptr<Lifetime> VariableLifetime::copy() const {
    return std::make_shared<VariableLifetime>(node, id);
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

VariableLifetime::VariableLifetime(const VariableNode *node, PlaceholderId id)
    : Lifetime(Lifetime::Kind::Variable, std::move(id)), node(node) { }

std::string ReferenceLifetime::toString() const {
    return fmt::format("&{}{}", placeholderString(), children->toString());
}

std::shared_ptr<Lifetime> ReferenceLifetime::copy() const {
    return std::make_shared<ReferenceLifetime>(std::make_shared<MultipleLifetime>(children->copy()), id);
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

ReferenceLifetime::ReferenceLifetime(std::shared_ptr<MultipleLifetime> lifetime, PlaceholderId id)
    : Lifetime(Lifetime::Kind::Reference, std::move(id)), children(std::move(lifetime)) { }
ReferenceLifetime::ReferenceLifetime(const ReferenceTypename &type, PlaceholderId id)
    : Lifetime(Lifetime::Kind::Reference, std::move(id)) {
    Typename &subType = *type.value;

    children = std::make_shared<MultipleLifetime>();
    children->push_back(makeAnonymousLifetime(subType, { id.first, id.second + 1 }));
}

std::shared_ptr<Lifetime> makeDefaultLifetime(const Typename &type, const PlaceholderId &id) {
    struct {
        const PlaceholderId &id;

        std::shared_ptr<Lifetime> operator()(const ReferenceTypename &type) const {
            return std::make_shared<ReferenceLifetime>(std::make_shared<MultipleLifetime>(), id);
        }

        std::shared_ptr<Lifetime> operator()(const StackTypename &) const {
            return nullptr;
        }

        std::shared_ptr<Lifetime> operator()(const FunctionTypename &) const {
            assert(false);
        }
    } visitor { id };

    return std::visit(visitor, type);
}

std::shared_ptr<Lifetime> makeAnonymousLifetime(const Typename &type, const PlaceholderId &id) {
    struct {
        const PlaceholderId &id;

        std::shared_ptr<Lifetime> operator()(const ReferenceTypename &type) const {
            return std::make_shared<ReferenceLifetime>(type, id);
        }

        std::shared_ptr<Lifetime> operator()(const StackTypename &) const {
            return std::make_shared<VariableLifetime>(nullptr, id);
        }

        std::shared_ptr<Lifetime> operator()(const FunctionTypename &) const {
            assert(false);
        }
    } visitor { id };

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

MultipleLifetime flatten(const std::vector<MultipleLifetime *> &lifetime) {
    MultipleLifetime result;

    for (MultipleLifetime *x : lifetime)
        result.insert(result.end(), x->begin(), x->end());

    return result;
}
