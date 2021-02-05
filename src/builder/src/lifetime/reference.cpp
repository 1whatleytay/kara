#include <builder/lifetime/reference.h>

#include <builder/lifetime/multiple.h>
#include <builder/lifetime/null.h>

#include <fmt/format.h>

std::string ReferenceLifetime::toString() const {
    return fmt::format("&{}{}", placeholderString(), children->toString());
}

std::shared_ptr<Lifetime> ReferenceLifetime::copy() const {
    return std::make_shared<ReferenceLifetime>(std::make_shared<MultipleLifetime>(children->copy()), id);
}

bool ReferenceLifetime::resolves(const BuilderScope &scope) const {
    return children->resolves(scope);
}

bool ReferenceLifetime::operator==(const Lifetime &lifetime) const {
    if (!Lifetime::operator==(lifetime))
        return false;

    auto refLifetime = dynamic_cast<const ReferenceLifetime &>(lifetime);

    return children->compare(*refLifetime.children);
}

std::shared_ptr<ReferenceLifetime> ReferenceLifetime::null() {
    return std::make_shared<ReferenceLifetime>(
        std::make_shared<MultipleLifetime>(MultipleLifetime {
            std::make_shared<NullLifetime>()
        }),
        PlaceholderId { nullptr, 0 }
    );
}

ReferenceLifetime::ReferenceLifetime(const ReferenceTypename &type, PlaceholderId id)
    : Lifetime(Lifetime::Kind::Reference, std::move(id)) {
    Typename &subType = *type.value;

    children = std::make_shared<MultipleLifetime>();

    if (auto x = makeAnonymousLifetime(subType, { id.first, id.second + 1 }))
        children->push_back(std::move(x));
}

ReferenceLifetime::ReferenceLifetime(std::shared_ptr<MultipleLifetime> lifetime, PlaceholderId id)
    : Lifetime(Lifetime::Kind::Reference, std::move(id)), children(std::move(lifetime)) { }
