#include <parser/typename.h>

#include <parser/number.h>

#include <fmt/format.h>

const Typename TypenameNode::any = StackTypename { "any" };
const Typename TypenameNode::null = StackTypename { "null" };
const Typename TypenameNode::integer = StackTypename { "int" };
const Typename TypenameNode::boolean = StackTypename { "bool" };
const Typename TypenameNode::nothing = StackTypename { "nothing" };

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
                fixedSize = n->value;
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
