#pragma once

#include <builder/lifetime/lifetime.h>

struct ReferenceLifetime : public Lifetime {
    std::shared_ptr<MultipleLifetime> children;

    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    bool operator==(const Lifetime &lifetime) const override;

    static std::shared_ptr<ReferenceLifetime> null();

    ReferenceLifetime(const ReferenceTypename &type, PlaceholderId id);
    ReferenceLifetime(std::shared_ptr<MultipleLifetime> lifetime, PlaceholderId id);
};
