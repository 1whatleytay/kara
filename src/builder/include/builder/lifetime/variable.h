#pragma once

#include <builder/lifetime/lifetime.h>

struct VariableLifetime : public Lifetime {
    const VariableNode *node = nullptr;

    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    bool operator==(const Lifetime &lifetime) const override;

    explicit VariableLifetime(const VariableNode *node, PlaceholderId id = { nullptr, 0 });
};
