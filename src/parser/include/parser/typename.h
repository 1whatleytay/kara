#pragma once

#include <parser/kinds.h>

#include <set>
#include <variant>

struct VariableNode;

struct StackTypename;
struct FunctionTypename;
struct ReferenceTypename;
using Typename = std::variant<StackTypename, FunctionTypename, ReferenceTypename>;

struct StackTypename {
    std::string value;

    bool operator==(const StackTypename &other) const;
    bool operator!=(const StackTypename &other) const;
};

using Constrictions = std::unordered_map<const VariableNode *, std::set<const VariableNode *>>;

struct FunctionTypename {
    enum class Kind {
        Regular,
        Pure,
        Pointer
    };

    Kind kind;
    std::shared_ptr<Typename> returnType;
    std::vector<Typename> parameters;
    Constrictions constrictions;

    bool operator==(const FunctionTypename &other) const;
    bool operator!=(const FunctionTypename &other) const;
};

struct ReferenceTypename {
    std::shared_ptr<Typename> value;

    bool operator==(const ReferenceTypename &other) const;
    bool operator!=(const ReferenceTypename &other) const;
};

struct TypenameNode : public Node {
    static const Typename nothing;
    static const Typename integer;

    Typename type;

    explicit TypenameNode(Node *parent);
};

std::string toString(const Typename &type);
