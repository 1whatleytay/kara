#include <parser/type.h>

#include <parser/variable.h>

namespace kara::parser {
    const hermes::Node *Type::alias() const {
        return isAlias ? children.front().get() : nullptr;
    }

    std::vector<const Variable *> Type::fields() const {
        std::vector<const Variable *> result(children.size());

        for (size_t a = 0; a < children.size(); a++)
            result[a] = children[a]->as<Variable>();

        return result;
    }

    Type::Type(Node *parent, bool external) : Node(parent, Kind::Type) {
        if (external)
            return;

        match("type", true);

        name = token();

        enum class Operators {
            NewClass,
            Alias
        };

        auto check = select<Operators>({ { "{", Operators::NewClass }, { "=", Operators::Alias } });

        switch (check) {
            case Operators::NewClass:
                while (!end() && !peek("}")) {
                    push<Variable>(false, false);

                    next(",");
                }

                needs("}");

                break;

            case Operators::Alias:
                isAlias = true;
                pushTypename(this);

                break;

            default:
                throw;
        }
    }
}
