#pragma once

#include <parser/typename.h>

#include <vector>
#include <variant>

struct VariableNode;

// AHHH I'm sorry....
struct BuilderScope;

struct Lifetime {
    enum class Kind {
        Variable,
        Reference
    };

    Kind kind = Kind::Variable;

    const VariableNode *placeholderVariable = nullptr;
    uint32_t placeholderUnique = 0;

    [[nodiscard]] std::string placeholderString() const;
    [[nodiscard]] virtual std::string toString() const = 0;
    [[nodiscard]] virtual std::shared_ptr<Lifetime> copy() const = 0;
    [[nodiscard]] virtual bool resolves(const BuilderScope &scope) const = 0;

    virtual bool operator==(const Lifetime &lifetime) const;
    bool operator!=(const Lifetime &lifetime) const;

    explicit Lifetime(Kind kind);
    Lifetime(Kind kind, const VariableNode *placeholder, uint32_t unique);

    virtual ~Lifetime() = default;
};

struct MultipleLifetime : public std::vector<std::shared_ptr<Lifetime>> {
    std::string toString() const;
    MultipleLifetime copy() const;
    bool compare(const MultipleLifetime &other) const;

    bool resolves(const BuilderScope &scope) const;

    MultipleLifetime() = default;
    MultipleLifetime(size_t size);
};

struct VariableLifetime : public Lifetime {
    const VariableNode *node = nullptr;

    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    bool operator==(const Lifetime &lifetime) const override;

    explicit VariableLifetime(const VariableNode *node,
        const VariableNode *placeholder = nullptr, uint32_t unique = 0);
};

struct ReferenceLifetime : public Lifetime {
    std::shared_ptr<MultipleLifetime> children;

    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    bool operator==(const Lifetime &lifetime) const override;

    explicit ReferenceLifetime(std::shared_ptr<MultipleLifetime> lifetime,
        const VariableNode *representing = nullptr, uint32_t unique = 0);
    ReferenceLifetime(const ReferenceTypename &type,
        const VariableNode *representing, uint32_t unique = 0);
};

// can return null
std::shared_ptr<Lifetime> makeDefaultLifetime(
    const Typename &type, const VariableNode *representing, uint32_t unique = 0);

// will always return value
std::shared_ptr<Lifetime> makeAnonymousLifetime(
    const Typename &type, const VariableNode *representing, uint32_t unique = 0);

using PlaceholderId = std::pair<const VariableNode *, uint32_t>;

struct PlaceholderIdHash {
    size_t operator()(const PlaceholderId &id) const {
        return std::hash<const VariableNode *>()(id.first) ^ std::hash<uint32_t>()(id.second);
    }
};

using LifetimeMatches = std::unordered_map<PlaceholderId, MultipleLifetime, PlaceholderIdHash>;
