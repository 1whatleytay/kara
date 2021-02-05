#pragma once

#include <builder/lifetime/lifetime.h>

#include <builder/lifetime/multiple.h>

struct ArrayLifetime : public Lifetime {
    MultipleLifetime defaultLifetime; // yikes

    std::vector<std::shared_ptr<MultipleLifetime>> possibleLifetimes;

    [[nodiscard]] MultipleLifetime merge() const;
    std::shared_ptr<MultipleLifetime> take();

//    void clean();

    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    bool operator==(const Lifetime &lifetime) const override;

    ArrayLifetime(const ArrayTypename &type, PlaceholderId id, const LifetimeCreator &creator);
    ArrayLifetime(std::vector<std::shared_ptr<MultipleLifetime>> possibleLifetimes,
        MultipleLifetime defaultLifetime, PlaceholderId id);
};
