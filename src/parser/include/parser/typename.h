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
    static const Typename any;
    static const Typename null;
    static const Typename nothing;
    static const Typename integer;
    static const Typename boolean;

    Typename type;

    explicit TypenameNode(Node *parent);
};

std::string toString(const ArrayTypename &type);
std::string toString(const StackTypename &type);
std::string toString(const FunctionTypename &type);
std::string toString(const ReferenceTypename &type);

std::string toString(const Typename &type);
