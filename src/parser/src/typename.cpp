#include <parser/typename.h>

#include <parser/number.h>

#include <fmt/format.h>

#include <array>

namespace types {
    // Special
    Typename any() { return StackTypename { "any" }; }
    Typename null() { return StackTypename { "null" }; }
    Typename nothing() { return StackTypename { "nothing" }; }

    // Numbers
    Typename boolean() { return StackTypename { "bool" }; }

    Typename i8() { return StackTypename { "byte" }; }
    Typename i16() { return StackTypename { "short" }; }
    Typename i32() { return StackTypename { "int" }; }
    Typename i64() { return StackTypename { "long" }; }

    Typename u8() { return StackTypename { "ubyte" }; }
    Typename u16() { return StackTypename { "ushort" }; }
    Typename u32() { return StackTypename { "uint" }; }
    Typename u64() { return StackTypename { "ulong" }; }

    Typename f32() { return StackTypename { "float" }; }
    Typename f64() { return StackTypename { "double" }; }

    bool isSigned(const Typename &type) {
        return type == i8() || type == i16() || type == i32() || type == i64();
    }
    bool isUnsigned(const Typename &type) {
        return type == u8() || type == u16() || type == u32() || type == u64();
    }
    bool isInteger(const Typename &type) {
        return isSigned(type) || isUnsigned(type);
    }
    bool isFloat(const Typename &type) {
        return type == f32() || type == f64();
    }

    bool isNumber(const Typename &type) {
        return isSigned(type) || isUnsigned(type) || isFloat(type);
    }

    int32_t priority(const Typename &type) {
        std::array<Typename, 10> types = {
            f64(), u64(), i64(),
            f32(), u32(), i32(),
            u16(), i16(),
            u8(), i8()
        };

        return types.size() - std::distance(types.begin(), std::find(types.begin(), types.end(), type));
    }
}

bool StackTypename::operator==(const StackTypename &other) const {
    return value == other.value;
}

bool StackTypename::operator!=(const StackTypename &other) const {
    return !operator==(other);
}

bool FunctionTypename::operator==(const FunctionTypename &other) const {
    return kind == other.kind
        && *returnType == *other.returnType
        && parameters == other.parameters;
}

bool FunctionTypename::operator!=(const FunctionTypename &other) const {
    return !operator==(other);
}

bool ReferenceTypename::operator==(const ReferenceTypename &other) const {
    return *value == *other.value;
}

bool ReferenceTypename::operator!=(const ReferenceTypename &other) const {
    return !operator==(other);
}

bool ArrayTypename::operator==(const ArrayTypename &other) const {
    return *value == *other.value
        && kind == other.kind
        && size == other.size;
}

bool ArrayTypename::operator!=(const ArrayTypename &other) const {
    return !operator==(other);
}

TypenameNode::TypenameNode(Node *parent) : Node(parent, Kind::Typename) {
    if (next("&")) {
        std::vector<std::string> options = { "let", "var" };
        size_t index = select(options, true, true);

        type = ReferenceTypename {
            std::make_unique<Typename>(std::move(pick<TypenameNode>()->type)),

            index != options.size() && index // isMutable?
        };
    } else if (next("[")) {
        Typename valueType = pick<TypenameNode>()->type;

        ArrayTypename::Kind kind = ArrayTypename::Kind::VariableSize;
        size_t fixedSize = 0;

        if (next(":")) {
            kind = ArrayTypename::Kind::Unbounded;

            if (next(":")) {
                kind = ArrayTypename::Kind::Iterable;
            } else if (std::unique_ptr<NumberNode> n = pick<NumberNode>(true)) {
                kind = ArrayTypename::Kind::FixedSize;
                assert(types::isSigned(n->type));

                fixedSize = n->value.i;
            }
        }

        type = ArrayTypename {
            kind,
            std::make_unique<Typename>(std::move(valueType)),
            fixedSize
        };

        needs("]");
    } else {
        type = StackTypename {
            token()
        };
    }
}

std::string toString(const ArrayTypename &type) {
    return fmt::format("[{}{}]", toString(*type.value),
        type.kind == ArrayTypename::Kind::Unbounded ? ":" :
        type.kind == ArrayTypename::Kind::Iterable ? "::" :
        type.kind == ArrayTypename::Kind::FixedSize ? fmt::format(":{}", type.size) :
        "");
}

std::string toString(const StackTypename &type) {
    return type.value;
}

std::string toString(const ReferenceTypename &type) {
    return fmt::format("&{}", toString(*type.value));
}

std::string toString(const FunctionTypename &type) {
    return "<func>";
}

std::string toString(const Typename &type) {
    return std::visit([](auto &type) { return toString(type); }, type);
}
