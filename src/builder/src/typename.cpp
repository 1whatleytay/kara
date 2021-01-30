#include <builder/builder.h>

#include <fmt/format.h>

Type *Builder::makeStackTypename(const StackTypename &name) {
    Typename type(name);

    if (type == TypenameNode::nothing)
        return Type::getVoidTy(context);

    if (type == TypenameNode::integer)
        return Type::getInt32Ty(context);

    if (type == TypenameNode::boolean)
        return Type::getInt1Ty(context);

    if (type == TypenameNode::null)
        return Type::getInt8PtrTy(context);

    if (type == TypenameNode::any)
        throw std::runtime_error("Any type is unsupported.");

    assert(false);
}

Type *Builder::makeTypename(const Typename &type) {
    struct {
        Builder &builder;

        Type *operator()(const StackTypename &type) {
            return builder.makeStackTypename(type);
        }

        Type *operator()(const ReferenceTypename &type) {
            return PointerType::get(builder.makeTypename(*type.value), 0);
        }

        Type *operator()(const ArrayTypename &type) {
            switch (type.kind) {
                case ArrayTypename::Kind::FixedSize:
                    return ArrayType::get(builder.makeTypename(*type.value), type.size);
                default:
                    throw std::runtime_error(fmt::format("Type {} is unimplemented.", toString(type)));
            }
        }

        Type *operator()(const FunctionTypename &type) {
            assert(false);
        }
    } visitor { *this };

    return std::visit(visitor, type);
}
