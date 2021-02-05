#include <builder/lifetime/null.h>

std::shared_ptr<Lifetime> NullLifetime::copy() const {
    return std::make_shared<NullLifetime>();
}

std::string NullLifetime::toString() const {
    return "null";
}

bool NullLifetime::resolves(const BuilderScope &scope) const {
    return false;
}

NullLifetime::NullLifetime() : Lifetime(Lifetime::Kind::Null) { }
