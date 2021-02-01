#pragma once

#include <parser/typename.h>

#include <vector>
#include <variant>

struct VariableNode;

// I'm sorry....
struct BuilderScope;

using PlaceholderId = std::pair<const VariableNode *, uint32_t>;

struct PlaceholderIdHash {
    size_t operator()(const PlaceholderId &id) const {
        return std::hash<const VariableNode *>()(id.first) ^ std::hash<uint32_t>()(id.second);
    }
};

struct Lifetime {
    enum class Kind {
        Variable,
        Reference
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

struct MultipleLifetime : public std::vector<std::shared_ptr<Lifetime>> {
    bool determinable = true;

    [[nodiscard]] std::string toString() const;
    [[nodiscard]] MultipleLifetime copy() const;
    [[nodiscard]] bool compare(const MultipleLifetime &other) const;

    [[nodiscard]] bool resolves(const BuilderScope &scope) const;

    void simplify();

    MultipleLifetime() = default;
    explicit MultipleLifetime(size_t size, bool determinable = true);
};

struct VariableLifetime : public Lifetime {
    const VariableNode *node = nullptr;

    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    bool operator==(const Lifetime &lifetime) const override;

    explicit VariableLifetime(const VariableNode *node, PlaceholderId id = { nullptr, 0 });
};

// Such a work around :||
using LifetimeCreator = std::function<std::shared_ptr<Lifetime>(const Typename &type, const PlaceholderId &id)>;

struct ReferenceLifetime : public Lifetime {
    std::shared_ptr<MultipleLifetime> children;

    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    bool operator==(const Lifetime &lifetime) const override;

    ReferenceLifetime(const ReferenceTypename &type, PlaceholderId id);
    ReferenceLifetime(const ArrayTypename &type, PlaceholderId id, const LifetimeCreator &creator);
    ReferenceLifetime(std::shared_ptr<MultipleLifetime> lifetime, PlaceholderId id);
};

// both can return null
std::shared_ptr<Lifetime> makeDefaultLifetime(const Typename &type, const PlaceholderId &id);
std::shared_ptr<Lifetime> makeAnonymousLifetime(const Typename &type, const PlaceholderId &id);

using LifetimeMatches = std::unordered_map<PlaceholderId, MultipleLifetime, PlaceholderIdHash>;

MultipleLifetime flatten(const std::vector<MultipleLifetime *> &lifetime);
