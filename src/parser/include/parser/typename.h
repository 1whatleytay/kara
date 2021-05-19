#pragma once

#include <parser/kinds.h>

#include <variant>

struct NumberNode;

enum class PrimitiveType {
    Any, Null, Nothing,
    Bool,
    Byte, Short, Int, Long,
    UByte, UShort, UInt, ULong,
    Float, Double,

    Unresolved
};

enum class ArrayKind {
    VariableSize, // [MyType]
    FixedSize, // [MyType:40]
    Unbounded, // [MyType:]
    Iterable, // [MyType::]
};

struct NamedTypenameNode : public Node {
    std::string name;

    explicit NamedTypenameNode(Node *parent, bool external = false);
};

struct PrimitiveTypenameNode : public Node {
    PrimitiveType type = PrimitiveType::Any;

    explicit PrimitiveTypenameNode(Node *parent, bool external = false);
};

struct ReferenceTypenameNode : public Node {
    bool isMutable = false;

    [[nodiscard]] const Node *body() const;

    explicit ReferenceTypenameNode(Node *parent, bool external = false);
};

struct OptionalTypenameNode : public Node {
    bool bubbles = false;

    [[nodiscard]] const Node *body() const;

    explicit OptionalTypenameNode(Node *parent, bool external = false);
};

struct ArrayTypenameNode : public Node {
    ArrayKind type = ArrayKind::VariableSize;

    [[nodiscard]] const Node *body() const;
    [[nodiscard]] const NumberNode *fixedSize() const;

    explicit ArrayTypenameNode(Node *parent, bool external = false);
};

void pushTypename(Node *parent);

// Builder Typename
struct NamedTypename;
struct ArrayTypename;
struct FunctionTypename;
struct OptionalTypename;
struct PrimitiveTypename;
struct ReferenceTypename;
using Typename = std::variant<
    NamedTypename,
    ArrayTypename,
    FunctionTypename,
    OptionalTypename,
    PrimitiveTypename,
    ReferenceTypename
>;

struct TypeNode;

struct PrimitiveTypename {
    PrimitiveType type = PrimitiveType::Any;

    [[nodiscard]] bool isSigned() const;
    [[nodiscard]] bool isUnsigned() const;
    [[nodiscard]] bool isInteger() const;
    [[nodiscard]] bool isFloat() const;

    [[nodiscard]] bool isNumber() const;

    [[nodiscard]] int32_t priority() const;

    static Typename from(PrimitiveType type);

    bool operator==(const PrimitiveTypename &other) const;
    bool operator!=(const PrimitiveTypename &other) const;
};

struct NamedTypename {
    const TypeNode *type = nullptr;

    bool operator==(const NamedTypename &other) const;
    bool operator!=(const NamedTypename &other) const;
};

struct FunctionTypename {
    enum class Kind {
//        Regular,
//        Pure,
        Pointer
    };

    Kind kind;
    std::shared_ptr<Typename> returnType;
    std::vector<Typename> parameters;

    bool operator==(const FunctionTypename &other) const;
    bool operator!=(const FunctionTypename &other) const;
};

struct ReferenceTypename {
    std::shared_ptr<Typename> value;

    bool isMutable = true;

    bool operator==(const ReferenceTypename &other) const;
    bool operator!=(const ReferenceTypename &other) const;
};

struct OptionalTypename {
    std::shared_ptr<Typename> value;

    bool bubbles = true;

    bool operator==(const OptionalTypename &other) const;
    bool operator!=(const OptionalTypename &other) const;
};

struct ArrayTypename {
    ArrayKind kind = ArrayKind::VariableSize;

    std::shared_ptr<Typename> value;

    size_t size = 0; // only for Kind::FixedSize

    bool operator==(const ArrayTypename &other) const;
    bool operator!=(const ArrayTypename &other) const;
};

std::string toString(const NamedTypename &type);
std::string toString(const ArrayTypename &type);
std::string toString(const FunctionTypename &type);
std::string toString(const PrimitiveTypename &type);
std::string toString(const ReferenceTypename &type);

std::string toString(const Typename &type);
