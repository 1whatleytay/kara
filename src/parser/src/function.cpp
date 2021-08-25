#include <parser/function.h>

#include <parser/expression.h>
#include <parser/scope.h>
#include <parser/typename.h>
#include <parser/variable.h>

namespace kara::parser {
    std::vector<const Variable *> Function::parameters() const {
        std::vector<const Variable *> result(parameterCount);

        for (size_t a = 0; a < parameterCount; a++)
            result[a] = children[a]->as<Variable>();

        return result;
    }

    const hermes::Node *Function::fixedType() const { return hasFixedType ? children[parameterCount].get() : nullptr; }

    const hermes::Node *Function::body() const {
        return isExtern ? nullptr : children[parameterCount + hasFixedType].get();
    }

    Function::Function(Node *parent, bool external)
        : Node(parent, Kind::Function) {
        if (external)
            return;

        name = token();

        if (next("(")) {
            while (!end() && !peek(")")) {
                push<Variable>(false, false);
                parameterCount++;

                next(",");
            }

            needs(")");
        }

        if (!(peek("{") || peek("=>") || peek("external"))) {
            pushTypename(this);
            hasFixedType = true;
        }

        if (next("external")) {
            match();
            isExtern = true;
        } else if (next("=>")) {
            match();
            push<Expression>();
        } else {
            match("{");

            push<Code>();

            needs("}");
        }
    }
}
