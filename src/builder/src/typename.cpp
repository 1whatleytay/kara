#include <builder/builder.h>

#include <builder/search.h>

#include <parser/type.h>

#include <fmt/format.h>
#include <builder/error.h>

Type *Builder::makeBuiltinTypename(const StackTypename &stack) {
    Typename type(stack);

    if (type == types::nothing())
        return Type::getVoidTy(*context);

    if (type == types::integer())
        return Type::getInt32Ty(*context);

    if (type == types::boolean())
        return Type::getInt1Ty(*context);

    if (type == types::null())
        return Type::getInt8PtrTy(*context);

    if (type == types::any())
        throw std::runtime_error("Any type is unsupported.");

    return nullptr;
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
