#pragma once

#include <vector>
#include <memory>
#include <string>
#include <variant>

namespace kara::parser {
    struct Type;
    struct Expression;
}

namespace kara::utils {
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

    enum class PrimitiveType {
        Any, Null, Nothing,
        Bool,
        Byte, Short, Int, Long,
        UByte, UShort, UInt, ULong,
        Float, Double
    };

    enum class ArrayKind {
        VariableSize, // [MyType]
        FixedSize, // [MyType:40]
        Unbounded, // [MyType:]
        UnboundedSized, // [MyType:expr]
        Iterable, // [MyType::]
    };

    enum class ReferenceKind {
        Regular, // &T
        Unique, // *T
        Shared, // *shared T
    };

    struct PrimitiveTypename {
        PrimitiveType type = PrimitiveType::Any;

        [[nodiscard]] bool isSigned() const;
        [[nodiscard]] bool isUnsigned() const;
        [[nodiscard]] bool isInteger() const;
        [[nodiscard]] bool isFloat() const;

        [[nodiscard]] bool isNumber() const;

        [[nodiscard]] int32_t size() const;
        [[nodiscard]] int32_t priority() const;

        static Typename from(PrimitiveType type);

        bool operator==(const PrimitiveTypename &other) const;
        bool operator!=(const PrimitiveTypename &other) const;
    };

    struct NamedTypename {
        std::string name; // original name
        const parser::Type *type = nullptr;

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
        std::vector<Typename> parameters;
        std::shared_ptr<Typename> returnType;

        bool operator==(const FunctionTypename &other) const;
        bool operator!=(const FunctionTypename &other) const;
    };

    struct ReferenceTypename {
        std::shared_ptr<Typename> value;

        bool isMutable = true;
        ReferenceKind kind = ReferenceKind::Regular;

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

        size_t size = 0; // only for ArrayKind::FixedSize
        const parser::Expression *expression = nullptr; // only for ArrayKind::UnboundedSized

        bool operator==(const ArrayTypename &other) const;
        bool operator!=(const ArrayTypename &other) const;
    };

    std::string toString(const NamedTypename &type);
    std::string toString(const ArrayTypename &type);
    std::string toString(const FunctionTypename &type);
    std::string toString(const PrimitiveTypename &type);
    std::string toString(const ReferenceTypename &type);

    std::string toString(const Typename &type);
}
