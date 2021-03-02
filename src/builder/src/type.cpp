#include <builder/builder.h>

#include <builder/error.h>

#include <parser/type.h>
#include <parser/variable.h>

void BuilderType::build() {
    std::vector<Type *> types;

    for (const auto &child : node->children) {
        const auto *e = child->as<VariableNode>();

        if (!e->fixedType)
            throw VerifyError(child.get(), "Every variable in type must have fixed type.");

        indices[e] = types.size();
        types.push_back(builder.makeTypename(e->fixedType.value()));
    }

    type->setBody(types);
}

BuilderType::BuilderType(const TypeNode *node, Builder &builder) : node(node), builder(builder) {
    type = StructType::create(*builder.context, { }, node->name);
}
