#pragma once

#include <builder/lifetime/lifetime.h>

struct NullLifetime : public Lifetime {
    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] std::shared_ptr<Lifetime> copy() const override;
    [[nodiscard]] bool resolves(const BuilderScope &scope) const override;

    NullLifetime();
};
