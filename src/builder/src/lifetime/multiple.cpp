#include <builder/lifetime/multiple.h>

#include <fmt/format.h>

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
        if (*operator[](a) != *other[a]) {
            return false;
        }
    }

    return true;
}

bool MultipleLifetime::resolves(const BuilderScope &scope) const {
    return std::all_of(begin(), end(),
        [&scope](const std::shared_ptr<Lifetime> &l) { return l->resolves(scope); });
}

void MultipleLifetime::simplify() {
    // really stupid unique algorithm, but it makes lifetimes look nice
    for (size_t a = 0; a < size(); a++) {
        const std::shared_ptr<Lifetime> &l = operator[](a);

        erase(std::remove_if(begin() + a + 1, end(), [&l](const std::shared_ptr<Lifetime> &k) {
            return k == l || *k == *l;
        }), end());
    }
}

MultipleLifetime::MultipleLifetime(size_t size)
    : std::vector<std::shared_ptr<Lifetime>>(size) { }

MultipleLifetime::MultipleLifetime(std::initializer_list<std::shared_ptr<Lifetime>> list)
    : std::vector<std::shared_ptr<Lifetime>>(list) { }