#include <parser/typename.h>

#include <parser/expression.h>
#include <parser/literals.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <fmt/format.h>

namespace kara::parser {
    NamedTypename::NamedTypename(Node *parent, bool external)
        : Node(parent, Kind::NamedTypename) {
        if (external)
            return;

        name = token();
    }

    PrimitiveTypename::PrimitiveTypename(Node *parent, bool external)
        : Node(parent, Kind::PrimitiveTypename) {
        if (external)
            return;

        /*
         * Any, Null, Nothing,
         * Bool,
         * Byte, Short, Int, Long,
         * UByte, UShort, UInt, ULong,
         * Float, Double
         */

        type = select<utils::PrimitiveType>(
            {
                { "any", utils::PrimitiveType::Any },
                { "null", utils::PrimitiveType::Null },
                { "nothing", utils::PrimitiveType::Nothing },
                { "bool", utils::PrimitiveType::Bool },
                { "byte", utils::PrimitiveType::Byte },
                { "short", utils::PrimitiveType::Short },
                { "int", utils::PrimitiveType::Int },
                { "long", utils::PrimitiveType::Long },
                { "ubyte", utils::PrimitiveType::UByte },
                { "ushort", utils::PrimitiveType::UShort },
                { "uint", utils::PrimitiveType::UInt },
                { "ulong", utils::PrimitiveType::ULong },
                { "float", utils::PrimitiveType::Float },
                { "double", utils::PrimitiveType::Double },
            },
            true);
    }

    const hermes::Node *ReferenceTypename::body() const { return children.front().get(); }

    ReferenceTypename::ReferenceTypename(Node *parent, bool external)
        : Node(parent, Kind::ReferenceTypename) {
        if (external)
            return;

        kind = select<utils::ReferenceKind>(
            { { "&", utils::ReferenceKind::Regular }, { "*", utils::ReferenceKind::Unique } }, false);
        match();

        if (next("shared", true)) {
            if (kind != utils::ReferenceKind::Unique)
                error("Shared pointer requested but base type is not unique.");

            kind = utils::ReferenceKind::Shared;
        }

        isMutable = decide({ { "let", false }, { "var", true } }, kind != utils::ReferenceKind::Regular, true);

        pushTypename(this);
    }

    const hermes::Node *OptionalTypename::body() const { return children.front().get(); }

    OptionalTypename::OptionalTypename(Node *parent, bool external)
        : Node(parent, Kind::OptionalTypename) {
        if (external)
            return;

        bubbles = select<bool>({ { "?", false }, { "!", true } }, false);

        pushTypename(this);
    }

    const hermes::Node *ArrayTypename::body() const { return children.front().get(); }

    const Number *ArrayTypename::fixedSize() const {
        return type == utils::ArrayKind::FixedSize ? children[1].get()->as<Number>() : nullptr;
    }

    const Expression *ArrayTypename::variableSize() const {
        return type == utils::ArrayKind::UnboundedSized ? children[1].get()->as<Expression>() : nullptr;
    }

    ArrayTypename::ArrayTypename(Node *parent, bool external)
        : Node(parent, Kind::ArrayTypename) {
        if (external)
            return;

        match("[");

        pushTypename(this);

        if (next(":")) {
            type = utils::ArrayKind::Unbounded;

            if (next(":")) {
                type = utils::ArrayKind::Iterable;
            } else {
                if (push<Number, Expression>(true)) {
                    switch (children.back()->is<Kind>()) {
                    case Kind::Number:
                        type = utils::ArrayKind::FixedSize;
                        break;

                    case Kind::Expression:
                        type = utils::ArrayKind::UnboundedSized;
                        break;

                    default:
                        throw;
                    }
                }
            }
        }

        needs("]");
    }

    std::vector<const hermes::Node *> FunctionTypename::parameters() const {
        std::vector<const Node *> result(children.size() - 1);

        std::transform(children.begin(), children.end() - 1, result.begin(), [](const auto &r) { return r.get(); });

        return result;
    }

    const hermes::Node *FunctionTypename::returnType() const { return children.back().get(); }

    FunctionTypename::FunctionTypename(Node *parent, bool external)
        : Node(parent, Kind::FunctionTypename) {
        if (external)
            return;

        match("func", true);

        if (next("locked", true))
            isLocked = true;

        if (next("ptr", true))
            kind = utils::FunctionKind::Pointer;

        needs("(");

        while (!end() && !peek(")")) {
            if (!push<Variable>(true))
                pushTypename(this);

            next(",");
        }

        needs(")");

        pushTypename(this);
    }

    void pushTypename(hermes::Node *parent) {
        parent->push<ReferenceTypename, OptionalTypename, ArrayTypename, PrimitiveTypename, FunctionTypename,
            NamedTypename>();
    }
}
