#include <builder/builder.h>

#include <builder/error.h>

#include <parser/type.h>
#include <parser/variable.h>

namespace kara::builder {
    void Type::build() {
        std::vector<llvm::Type *> types;

        auto fields = node->fields();

        for (auto child : fields) {
            if (!child->hasFixedType)
                throw VerifyError(child, "Every variable in type must have fixed type.");

            indices[child] = types.size();
            types.push_back(builder.makeTypename(builder.resolveTypename(child->fixedType())));
        }

        type->setBody(types);

        // might cause infinite loop, move to build?
        auto needsImplicitDestructor = std::any_of(fields.begin(), fields.end(), [this](auto field) {
            assert(field->hasFixedType);

            auto fieldType = field->fixedType();

            return builder.needsDestroy(builder.resolveTypename(fieldType));
        });

        if (needsImplicitDestructor) {
            auto ptr = std::make_unique<builder::Function>(node, builder);
            implicitDestructor = ptr.get();

            // huh, why do i do this? shouldn't i just be checking the type, or else it wont build it?
            // i should delete this!!
            builder.implicitDestructors[node] = std::move(ptr);

            implicitDestructor->build();
        }
    }

    Type::Type(const parser::Type *node, builder::Builder &builder)
        : node(node)
        , builder(builder) {
        assert(!node->isAlias);

        type = llvm::StructType::create(builder.context, {}, node->name);

        auto fields = node->fields();
    }
}
