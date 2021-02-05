#include <builder/lifetime/lifetime.h>

#include <builder/builder.h>
#include <builder/lifetime/null.h>
#include <builder/lifetime/variable.h>
#include <builder/lifetime/reference.h>
#include <builder/lifetime/array.h>

#include <parser/variable.h>

#include <fmt/format.h>

bool Lifetime::operator==(const Lifetime &lifetime) const {
    return kind == lifetime.kind && id == lifetime.id;
}

bool Lifetime::operator!=(const Lifetime &lifetime) const {
    return !operator==(lifetime);
}

std::string Lifetime::placeholderString() const {
    return id.first ? fmt::format("({}.{})", id.first->name, id.second) : "";
}

Lifetime::Lifetime(Kind kind) : kind(kind) { }
Lifetime::Lifetime(Kind kind, PlaceholderId id) : kind(kind), id(std::move(id)) { }

std::shared_ptr<Lifetime> makeDefaultLifetime(const Typename &type, const PlaceholderId &id) {
    struct {
        const PlaceholderId &id;

        std::shared_ptr<Lifetime> operator()(const ReferenceTypename &type) const {
            return std::make_shared<ReferenceLifetime>(std::make_shared<MultipleLifetime>(MultipleLifetime {
                std::make_shared<NullLifetime>()
            }), id);
        }

        std::shared_ptr<Lifetime> operator()(const StackTypename &) const {
            return nullptr;
        }

        std::shared_ptr<Lifetime> operator()(const FunctionTypename &) const {
            assert(false);
        }

        std::shared_ptr<Lifetime> operator()(const ArrayTypename &type) const {
//            return makeDefaultLifetime(*type.value, id);
//            return nullptr;
            return std::make_shared<ArrayLifetime>(type, id, makeDefaultLifetime);
        }
    } visitor { id };

    return std::visit(visitor, type);
}

std::shared_ptr<Lifetime> makeAnonymousLifetime(const Typename &type, const PlaceholderId &id) {
    struct {
        const PlaceholderId &id;

        std::shared_ptr<Lifetime> operator()(const ReferenceTypename &type) const {
            return std::make_shared<ReferenceLifetime>(type, id);
        }

        std::shared_ptr<Lifetime> operator()(const StackTypename &) const {
            return id.first ? std::make_shared<VariableLifetime>(nullptr, id) : nullptr;
        }

        std::shared_ptr<Lifetime> operator()(const FunctionTypename &) const {
            assert(false);
        }

        std::shared_ptr<Lifetime> operator()(const ArrayTypename &type) const {
            return std::make_shared<ArrayLifetime>(type, id, makeAnonymousLifetime);

//            return makeAnonymousLifetime(*type.value, id);
//            return id.first ? std::make_shared<VariableLifetime>(nullptr, id) : nullptr;
//            return std::make_shared<ReferenceLifetime>(type, id);
        }
    } visitor { id };

    return std::visit(visitor, type);
}

MultipleLifetime flatten(const std::vector<MultipleLifetime *> &lifetime) {
    MultipleLifetime result;

    for (MultipleLifetime *x : lifetime)
        result.insert(result.end(), x->begin(), x->end());

    return result;
}
