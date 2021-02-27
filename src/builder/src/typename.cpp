#include <builder/builder.h>

#include <builder/search.h>

#include <parser/type.h>

#include <fmt/format.h>
#include <builder/error.h>

Type *Builder::makeBuiltinTypename(const StackTypename &stack) const {
    Typename type(stack);

    std::vector<std::pair<Typename, Type *>> typeMap = {
        { types::nothing(), Type::getVoidTy(*context) },
        { types::i8(), Type::getInt8Ty(*context) },
        { types::i16(), Type::getInt16Ty(*context) },
        { types::i32(), Type::getInt32Ty(*context) },
        { types::i64(), Type::getInt64Ty(*context) },
        { types::u8(), Type::getInt8Ty(*context) },
        { types::u16(), Type::getInt16Ty(*context) },
        { types::u32(), Type::getInt32Ty(*context) },
        { types::u64(), Type::getInt64Ty(*context) },
        { types::f32(), Type::getFloatTy(*context) },
        { types::f64(), Type::getDoubleTy(*context) },
        { types::boolean(), Type::getInt1Ty(*context) },
        { types::null(), Type::getInt8PtrTy(*context) },
    };

    if (type == types::any())
        throw std::runtime_error("Any type is unsupported.");

    auto iterator = std::find_if(typeMap.begin(), typeMap.end(), [&type](const auto &e) {
        return e.first == type;
    });

    return iterator == typeMap.end() ? nullptr : iterator->second;
}

Type *Builder::makeStackTypename(const StackTypename &type, const Node *node) {
    Type *builtin = makeBuiltinTypename(type);

    if (builtin)
        return builtin;

    const auto *found = Builder::find(type, node);

    if (!found)
        throw VerifyError(node, "Failed to find type matching {}.", type.value);

    return makeType(found)->type;
}

Type *Builder::makeTypename(const Typename &type, const Node *node) {
    struct {
        Builder &builder;
        const Node *node;

        Type *operator()(const StackTypename &type) {
            return builder.makeStackTypename(type, node);
        }

        Type *operator()(const ReferenceTypename &type) {
            return PointerType::get(builder.makeTypename(*type.value, node), 0);
        }

        Type *operator()(const ArrayTypename &type) {
            switch (type.kind) {
                case ArrayTypename::Kind::FixedSize:
                    return ArrayType::get(builder.makeTypename(*type.value, node), type.size);
                default:
                    throw std::runtime_error(fmt::format("Type {} is unimplemented.", toString(type)));
            }
        }

        Type *operator()(const FunctionTypename &type) {
            assert(false);
        }
    } visitor { *this, node };

    return std::visit(visitor, type);
}
