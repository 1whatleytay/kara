#pragma once

#include <parser/typename.h>

#include <vector>
#include <variant>

struct VariableNode;
struct BuilderScope;

using PlaceholderId = std::pair<const VariableNode *, uint32_t>;

struct PlaceholderIdHash {
    size_t operator()(const PlaceholderId &id) const {
        return std::hash<const VariableNode *>()(id.first) ^ std::hash<uint32_t>()(id.second);
    }
};

struct Lifetime {
    enum class Kind {
        Null,
        Variable,
        Reference,
        Array,
    };

    Kind kind = Kind::Variable;

    PlaceholderId id;

    [[nodiscard]] std::string placeholderString() const;
    [[nodiscard]] virtual std::string toString() const = 0;
    [[nodiscard]] virtual std::shared_ptr<Lifetime> copy() const = 0;
    [[nodiscard]] virtual bool resolves(const BuilderScope &scope) const = 0;

    virtual bool operator==(const Lifetime &lifetime) const;
    bool operator!=(const Lifetime &lifetime) const;

    explicit Lifetime(Kind kind);
    Lifetime(Kind kind, PlaceholderId id);

    virtual ~Lifetime() = default;
};

// Such a work around :||
using LifetimeCreator = std::function<std::shared_ptr<Lifetime>(const Typename &type, const PlaceholderId &id)>;

// both can return null
std::shared_ptr<Lifetime> makeDefaultLifetime(const Typename &type, const PlaceholderId &id);
std::shared_ptr<Lifetime> makeAnonymousLifetime(const Typename &type, const PlaceholderId &id);

using LifetimeMatches = std::unordered_map<PlaceholderId, MultipleLifetime, PlaceholderIdHash>;

MultipleLifetime flatten(const std::vector<MultipleLifetime *> &lifetime);
