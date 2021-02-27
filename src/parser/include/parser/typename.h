#pragma once

#include <parser/kinds.h>

#include <set>
#include <variant>

struct VariableNode;

struct ArrayTypename;
struct StackTypename;
struct FunctionTypename;
struct ReferenceTypename;
using Typename = std::variant<ArrayTypename, StackTypename, FunctionTypename, ReferenceTypename>;

struct StackTypename {
    std::string value;

    bool operator==(const StackTypename &other) const;
    bool operator!=(const StackTypename &other) const;
};

struct FunctionTypename {
    enum class Kind {
        Regular,
        Pure,
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

    bool isMutable = false;

    bool operator==(const ReferenceTypename &other) const;
    bool operator!=(const ReferenceTypename &other) const;
};

struct ArrayTypename {
    enum class Kind {
        VariableSize, // [MyType]
        FixedSize, // [MyType:40]
        Unbounded, // [MyType:]
        Iterable, // [MyType::]
    };

    Kind kind = Kind::VariableSize;

    std::shared_ptr<Typename> value;

    size_t size = 0; // only for Kind::FixedSize

    bool operator==(const ArrayTypename &other) const;
    bool operator!=(const ArrayTypename &other) const;
};

struct TypenameNode : public Node {
    Typename type;

    explicit TypenameNode(Node *parent);
};

std::string toString(const ArrayTypename &type);
std::string toString(const StackTypename &type);
std::string toString(const FunctionTypename &type);
std::string toString(const ReferenceTypename &type);

std::string toString(const Typename &type);

namespace types {
    Typename any();
    Typename null();
    Typename nothing();

    Typename boolean();

    Typename i8();
    Typename i16();
    Typename i32();
    Typename i64();

    Typename u8();
    Typename u16();
    Typename u32();
    Typename u64();

    Typename f32();
    Typename f64();

    bool isSigned(const Typename &type);
    bool isUnsigned(const Typename &type);
    bool isInteger(const Typename &type);
    bool isFloat(const Typename &type);

    bool isNumber(const Typename &type);

    int32_t priority(const Typename &type);
}
