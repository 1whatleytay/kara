#include <builder/builder.h>

Type *Builder::makeStackTypename(const StackTypename &name) {
    Typename type(name);

    if (type == TypenameNode::nothing)
        return Type::getVoidTy(context);

    if (type == TypenameNode::integer)
        return Type::getInt32Ty(context);

    if (type == TypenameNode::boolean)
        return Type::getInt1Ty(context);

    assert(false);
}

Type *Builder::makeTypename(const Typename &type) {
    if (std::holds_alternative<StackTypename>(type)) {
        return makeStackTypename(std::get<StackTypename>(type));
    } else if (std::holds_alternative<ReferenceTypename>(type)) {
        return PointerType::get(makeTypename(*std::get<ReferenceTypename>(type).value), 0);
    }

    throw std::runtime_error("Cannot match type.");
}
