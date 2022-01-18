#include <utils/typename.h>

#include <fmt/format.h>

namespace kara::utils {
    bool PrimitiveTypename::operator==(const PrimitiveTypename &other) const { return type == other.type; }

    bool PrimitiveTypename::operator!=(const PrimitiveTypename &other) const { return !operator==(other); }

    bool NamedTypename::operator==(const NamedTypename &other) const { return type == other.type; }

    bool NamedTypename::operator!=(const NamedTypename &other) const { return !operator==(other); }

    bool FunctionTypename::operator==(const FunctionTypename &other) const {
        auto check = [this, &other]() -> bool {
            if (parameters.size() != other.parameters.size())
                return false;

            // TODO: find some elegant solution for names and func pointers
            for (size_t a = 0; a < parameters.size(); a++) {
                if (parameters[a].second != other.parameters[a].second) {
                    return false;
                }
            }

            return true;
        };

        return kind == other.kind && *returnType == *other.returnType && check();
    }

    bool FunctionTypename::operator!=(const FunctionTypename &other) const { return !operator==(other); }

    bool ReferenceTypename::operator==(const ReferenceTypename &other) const {
        return *value == *other.value && isMutable == other.isMutable && kind == other.kind;
    }

    bool ReferenceTypename::operator!=(const ReferenceTypename &other) const { return !operator==(other); }

    bool OptionalTypename::operator==(const OptionalTypename &other) const {
        return *value == *other.value && bubbles == other.bubbles;
    }

    bool OptionalTypename::operator!=(const OptionalTypename &other) const { return !operator==(other); }

    bool ArrayTypename::operator==(const ArrayTypename &other) const {
        return *value == *other.value && kind == other.kind && size == other.size && expression == other.expression;
    }

    bool ArrayTypename::operator!=(const ArrayTypename &other) const { return !operator==(other); }

    std::string toString(const ArrayTypename &type) {
        std::string end = ([&type]() -> std::string {
            switch (type.kind) {
            case ArrayKind::Unbounded:
                return ":";
            case ArrayKind::Iterable:
                return "::";
            case ArrayKind::FixedSize:
                return fmt::format(":{}", type.size);
            case ArrayKind::UnboundedSized:
                return fmt::format(":expr");
            case ArrayKind::VariableSize:
                return "";
            default:
                throw;
            }
        })();

        return fmt::format("[{}{}]", toString(*type.value), end);
    }

    std::string toString(const PrimitiveTypename &type) {
        switch (type.type) {
        case PrimitiveType::Any:
            return "any";
        case PrimitiveType::Null:
            return "null";
        case PrimitiveType::Nothing:
            return "nothing";
        case PrimitiveType::Bool:
            return "bool";
        case PrimitiveType::Byte:
            return "byte";
        case PrimitiveType::Short:
            return "short";
        case PrimitiveType::Int:
            return "int";
        case PrimitiveType::Long:
            return "long";
        case PrimitiveType::UByte:
            return "ubyte";
        case PrimitiveType::UShort:
            return "ushort";
        case PrimitiveType::UInt:
            return "uint";
        case PrimitiveType::ULong:
            return "ulong";
        case PrimitiveType::Float:
            return "float";
        case PrimitiveType::Double:
            return "double";
        default:
            throw;
        }
    }

    std::string toString(const NamedTypename &type) { return type.name; }

    std::string toString(const OptionalTypename &type) {
        return fmt::format("{}{}", type.bubbles ? "!" : "?", toString(*type.value));
    }

    std::string toString(const ReferenceTypename &type) {
        auto prefix = ([type]() {
            switch (type.kind) {
            case ReferenceKind::Regular:
                return "&";
            case ReferenceKind::Unique:
                return "*";
            case ReferenceKind::Shared:
                return "*shared ";
            default:
                throw;
            }
        })();

        auto mutability = ([type]() -> std::string {
            switch (type.kind) {
            case ReferenceKind::Regular:
                return type.isMutable ? "var " : "";
            case ReferenceKind::Unique:
            case ReferenceKind::Shared:
                return type.isMutable ? "" : "let ";
            default:
                throw;
            }
        })();

        return fmt::format("{}{}{}", prefix, mutability, toString(*type.value));
    }

    std::string toString(const FunctionTypename &type) {
        auto heading = ([type]() -> std::string {
            switch (type.kind) {
            case FunctionKind::Pointer:
                return "func ptr";
            case FunctionKind::Regular:
                return "func";
            default:
                throw;
            }
        })();

        std::vector<std::string> types(type.parameters.size());
        std::transform(type.parameters.begin(), type.parameters.end(), types.begin(), [](const auto &t) {
            return toString(t.second);
        });

        return fmt::format("{}({}) {}", heading, fmt::join(types, ", "), toString(*type.returnType));
    }

    std::string toString(const Typename &type) {
        return std::visit([](auto &type) { return toString(type); }, type);
    }

    bool PrimitiveTypename::isSigned() const {
        return type == PrimitiveType::Byte || type == PrimitiveType::Short || type == PrimitiveType::Int
            || type == PrimitiveType::Long;
    }
    bool PrimitiveTypename::isUnsigned() const {
        return type == PrimitiveType::UByte || type == PrimitiveType::UShort || type == PrimitiveType::UInt
            || type == PrimitiveType::ULong;
    }

    bool PrimitiveTypename::isInteger() const { return isSigned() || isUnsigned(); }

    bool PrimitiveTypename::isFloat() const { return type == PrimitiveType::Float || type == PrimitiveType::Double; }

    bool PrimitiveTypename::isNumber() const { return isInteger() || isFloat(); }

    int32_t PrimitiveTypename::size() const {
        switch (type) {
        case PrimitiveType::ULong:
        case PrimitiveType::Long:
        case PrimitiveType::Double:
            return 8;
        case PrimitiveType::UInt:
        case PrimitiveType::Int:
        case PrimitiveType::Float:
            return 4;
        case PrimitiveType::UShort:
        case PrimitiveType::Short:
            return 2;
        case PrimitiveType::UByte:
        case PrimitiveType::Byte:
            return 1;
        default:
            return -1;
        }
    }

    int32_t PrimitiveTypename::priority() const {
        std::array<PrimitiveType, 10> types = {
            PrimitiveType::Double,
            PrimitiveType::ULong,
            PrimitiveType::Long,
            PrimitiveType::Float,
            PrimitiveType::UInt,
            PrimitiveType::Int,
            PrimitiveType::UShort,
            PrimitiveType::Short,
            PrimitiveType::UByte,
            PrimitiveType::Byte,
        };

        auto iterator = std::find(types.begin(), types.end(), type);

        if (iterator == types.end())
            return -1;

        return static_cast<int32_t>(types.size() - std::distance(types.begin(), iterator));
    }

    Typename from(PrimitiveType type) { return Typename { PrimitiveTypename { type } }; }
}