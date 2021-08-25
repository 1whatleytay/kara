#include <parser/variable.h>

#include <parser/expression.h>
#include <parser/literals.h>

namespace kara::parser {
    const hermes::Node *Variable::fixedType() const { return hasFixedType ? children[0].get() : nullptr; }

    const Expression *Variable::value() const {
        return hasInitialValue ? children[hasFixedType]->as<Expression>() : nullptr;
    }

    const Number *Variable::constantValue() const {
        return hasConstantValue ? children[hasFixedType]->as<Number>() : nullptr;
    }

    Variable::Variable(Node *parent, bool isExplicit, bool external)
        : Node(parent, Kind::Variable) {
        if (external)
            return;

        hermes::SelectMap<bool> mutabilityMap = { { "let", false }, { "var", true } };

        std::optional<bool> mutability = isExplicit ? select(mutabilityMap, true) : maybe(mutabilityMap, true);

        if (mutability) {
            match();
            isMutable = mutability.value(); // analysis says "The address of a the local
                                            // variable may escape the function"
                                            // ^^^ i call bs
        }

        name = token();

        if (next("=")) {
            if (parent && parent->is(Kind::Root) && push<Number>(true)) {
                hasConstantValue = true;
            } else {
                push<Expression>();

                hasInitialValue = true;
            }
        } else {
            hasFixedType = true;
            pushTypename(this);

            if (next("external", true)) {
                isExternal = true;
            } else if (next("=")) {
                if (parent && parent->is(Kind::Root) && push<Number>(true)) {
                    hasConstantValue = true;
                } else {
                    push<Expression>();

                    hasInitialValue = true;
                }
            }
        }
    }
}
