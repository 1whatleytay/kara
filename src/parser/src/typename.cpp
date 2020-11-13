#include <parser/typename.h>

#include <fmt/format.h>

const Typename TypenameNode::nothing = StackTypename { "nothing" };
const Typename TypenameNode::integer = StackTypename { "int" };

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

TypenameNode::TypenameNode(Node *parent) : Node(parent, Kind::Typename) {
    if (next("&")) {
        type = ReferenceTypename {
            std::make_unique<Typename>(std::move(pick<TypenameNode>()->type))
        };
    } else {
        type = StackTypename {
            token()
        };
    }
}

std::string toString(const Typename &type) {
    struct {
        std::string operator()(const StackTypename &type) {
            return type.value;
        }

        std::string operator()(const ReferenceTypename &type) {
            return fmt::format("&{}", toString(*type.value));
        }

        std::string operator()(const FunctionTypename &type) {
            return "<func>";
        }
    } stringifier;

    return std::visit(stringifier, type);
}
