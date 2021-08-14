#include <builder/builder.h>

#include <builder/error.h>

#include <parser/type.h>
#include <parser/variable.h>

void BuilderType::build() {
    std::vector<Type *> types;

    auto fields = node->fields();

    for (auto child : fields) {
        if (!child->hasFixedType)
            throw VerifyError(child, "Every variable in type must have fixed type.");

        indices[child] = types.size();
        types.push_back(builder.makeTypename(builder.resolveTypename(child->fixedType())));
    }

    type->setBody(types);

    implicitDestructor->build();
}

BuilderType::BuilderType(const TypeNode *node, Builder &builder) : node(node), builder(builder) {
    assert(!node->isAlias);

    type = StructType::create(builder.context, { }, node->name);

    auto ptr = std::make_unique<BuilderFunction>(node, builder);
    implicitDestructor = ptr.get();

    builder.implicitDestructors[node] = std::move(ptr);
}
