#pragma once

#include <builder/lifetime/lifetime.h>

struct MultipleLifetime : public std::vector<std::shared_ptr<Lifetime>> {
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] MultipleLifetime copy() const;
    [[nodiscard]] bool compare(const MultipleLifetime &other) const;

    [[nodiscard]] bool resolves(const BuilderScope &scope) const;

    void simplify();

    MultipleLifetime() = default;
    explicit MultipleLifetime(size_t size);
    MultipleLifetime(std::initializer_list<std::shared_ptr<Lifetime>> list);
};
