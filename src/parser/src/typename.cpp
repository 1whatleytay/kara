#include <parser/typename.h>

#include <parser/type.h>
#include <parser/literals.h>

#include <fmt/format.h>

NamedTypenameNode::NamedTypenameNode(Node *parent, bool external) : Node(parent, Kind::NamedTypename) {
    if (external)
        return;

    name = token();
}

PrimitiveTypenameNode::PrimitiveTypenameNode(Node *parent, bool external) : Node(parent, Kind::PrimitiveTypename) {
    if (external)
        return;

    /*
     * Any, Null, Nothing,
     * Bool,
     * Byte, Short, Int, Long,
     * UByte, UShort, UInt, ULong,
     * Float, Double
     */

    type = select<PrimitiveType>({
        "any", "null", "nothing",
        "bool",
        "byte", "short", "int", "long",
        "ubyte", "ushort", "uint", "ulong",
        "float", "double"
    });
}

const Node *ReferenceTypenameNode::body() const {
    return children.front().get();
}

ReferenceTypenameNode::ReferenceTypenameNode(Node *parent, bool external) : Node(parent, Kind::ReferenceTypename) {
    if (external)
        return;

    match("&");

    isMutable = select({ "let", "var" }, true, true) == 1;

    pushTypename(this);
}

const Node *OptionalTypenameNode::body() const {
    return children.front().get();
}

OptionalTypenameNode::OptionalTypenameNode(Node *parent, bool external) : Node(parent, Kind::OptionalTypename) {
    if (external)
        return;

    bubbles = select<bool>({ "?", "!" });

    pushTypename(this);
}

const Node *ArrayTypenameNode::body() const {
    return children.front().get();
}

const NumberNode *ArrayTypenameNode::fixedSize() const {
    return type == ArrayKind::FixedSize ? children[1]->as<NumberNode>() : nullptr;
}

ArrayTypenameNode::ArrayTypenameNode(Node *parent, bool external) : Node(parent, Kind::ArrayTypename) {
    if (external)
        return;

    match("[");

    pushTypename(this);

    if (next(":")) {
        type = ArrayKind::Unbounded;

        if (next(":")) {
            type = ArrayKind::Iterable;
        } else {
            type = ArrayKind::FixedSize;

            push<NumberNode>(true);
        }
    }

    needs("]");
}

void pushTypename(Node *parent) {
    parent->push<
        ReferenceTypenameNode,
        OptionalTypenameNode,
        ArrayTypenameNode,
        PrimitiveTypenameNode,
        NamedTypenameNode
    >();
}

bool PrimitiveTypename::operator==(const PrimitiveTypename &other) const {
    return type == other.type;
}

bool PrimitiveTypename::operator!=(const PrimitiveTypename &other) const {
    return !operator==(other);
}

bool NamedTypename::operator==(const NamedTypename &other) const {
    return type == other.type;
}

bool NamedTypename::operator!=(const NamedTypename &other) const {
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

bool OptionalTypename::operator==(const OptionalTypename &other) const {
    return *value == *other.value && bubbles == other.bubbles;
}

bool OptionalTypename::operator!=(const OptionalTypename &other) const {
    return !operator==(other);
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


std::string toString(const ArrayTypename &type) {
    return fmt::format("[{}{}]", toString(*type.value),
        type.kind == ArrayKind::Unbounded ? ":" :
            type.kind == ArrayKind::Iterable ? "::" :
                type.kind == ArrayKind::FixedSize ? fmt::format(":{}", type.size) :
                    "");
}

std::string toString(const PrimitiveTypename &type) {
    switch (type.type) {
        case PrimitiveType::Any: return "any";
        case PrimitiveType::Null: return "null";
        case PrimitiveType::Nothing: return "nothing";
        case PrimitiveType::Bool: return "bool";
        case PrimitiveType::Byte: return "byte";
        case PrimitiveType::Short: return "short";
        case PrimitiveType::Int: return "int";
        case PrimitiveType::Long: return "long";
        case PrimitiveType::UByte: return "ubyte";
        case PrimitiveType::UShort: return "ushort";
        case PrimitiveType::UInt: return "uint";
        case PrimitiveType::ULong: return "ulong";
        case PrimitiveType::Float: return "float";
        case PrimitiveType::Double: return "double";
        default:
            assert(false);
    }
}

std::string toString(const NamedTypename &type) {
    return type.type->name;
}

std::string toString(const OptionalTypename &type) {
    return fmt::format("{}{}", type.bubbles ? "!" : "?", toString(*type.value));
}

std::string toString(const ReferenceTypename &type) {
    return fmt::format("&{}{}", type.isMutable ? "var " : "", toString(*type.value));
}

std::string toString(const FunctionTypename &type) {
    return "<func>";
}

std::string toString(const Typename &type) {
    return std::visit([](auto &type) { return toString(type); }, type);
}

bool PrimitiveTypename::isSigned() const {
    return type == PrimitiveType::Byte
        || type == PrimitiveType::Short
        || type == PrimitiveType::Int
        || type == PrimitiveType::Long;
}
bool PrimitiveTypename::isUnsigned() const {
    return type == PrimitiveType::UByte
        || type == PrimitiveType::UShort
        || type == PrimitiveType::UInt
        || type == PrimitiveType::ULong;
}

bool PrimitiveTypename::isInteger() const {
    return isSigned() || isUnsigned();
}

bool PrimitiveTypename::isFloat() const {
    return type == PrimitiveType::Float || type == PrimitiveType::Double;
}

bool PrimitiveTypename::isNumber() const {
    return isInteger() || isFloat();
}

int32_t PrimitiveTypename::priority() const {
    std::array<PrimitiveType, 10> types = {
        PrimitiveType::Double, PrimitiveType::ULong, PrimitiveType::Long,
        PrimitiveType::Float, PrimitiveType::UInt, PrimitiveType::Int,
        PrimitiveType::UShort, PrimitiveType::Short,
        PrimitiveType::UByte, PrimitiveType::Byte
    };

    auto iterator = std::find(types.begin(), types.end(), type);

    if (iterator == types.end())
        return -1;

    return static_cast<int32_t>(types.size() - std::distance(types.begin(), iterator));
}

Typename PrimitiveTypename::from(PrimitiveType type) {
    return Typename { PrimitiveTypename { type } };
}
