#include <builder/lifetime/array.h>

#include <fmt/format.h>

MultipleLifetime ArrayLifetime::merge() const {
    // An *attempt* to clean things up, initialized in constructor.
    if (possibleLifetimes.empty())
        return defaultLifetime;

    MultipleLifetime result;

    for (const auto &l : possibleLifetimes) {
        result.insert(result.end(), l->begin(), l->end());
    }

    result.simplify();

    return result;
}

std::shared_ptr<MultipleLifetime> ArrayLifetime::take() {
    auto ptr = std::make_shared<MultipleLifetime>(merge().copy());

    possibleLifetimes.push_back(ptr);

    return ptr;
}

//void ArrayLifetime::clean() {
//    // Call when bad things happen.
//    possibleLifetimes = { std::make_shared<MultipleLifetime>(merge()) };
//}

std::string ArrayLifetime::toString() const {
    return fmt::format("[]{}{}", placeholderString(), merge().toString());
}

std::shared_ptr<Lifetime> ArrayLifetime::copy() const {
    std::vector<std::shared_ptr<MultipleLifetime>> copies;
    copies.reserve(possibleLifetimes.size());

    // I can feel the heap cry as I write this code...
    // And also the algorithm experts...
    for (const auto &l : possibleLifetimes)
        copies.push_back(std::make_shared<MultipleLifetime>(l->copy()));

    return std::make_shared<ArrayLifetime>(std::move(copies), defaultLifetime, id);
}

bool ArrayLifetime::resolves(const BuilderScope &scope) const {
    return merge().resolves(scope);
}

bool ArrayLifetime::operator==(const Lifetime &lifetime) const {
    if (!Lifetime::operator==(lifetime))
        return false;

    auto arrLifetime = dynamic_cast<const ArrayLifetime &>(lifetime);

    return merge().compare(arrLifetime.merge());
}

ArrayLifetime::ArrayLifetime(const ArrayTypename &type, PlaceholderId id, const LifetimeCreator &creator)
    : Lifetime(Lifetime::Kind::Array, std::move(id)) {
    Typename &subType = *type.value;

    if (auto x = creator(subType, { id.first, id.second + 1 }))
        defaultLifetime.push_back(std::move(x));
}

ArrayLifetime::ArrayLifetime(std::vector<std::shared_ptr<MultipleLifetime>> possibleLifetimes,
    MultipleLifetime defaultLifetime, PlaceholderId id)
    : Lifetime(Lifetime::Kind::Array, std::move(id)),
    possibleLifetimes(std::move(possibleLifetimes)),
    defaultLifetime(std::move(defaultLifetime)) { }