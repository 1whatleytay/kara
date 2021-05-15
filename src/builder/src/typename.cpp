#include <builder/builder.h>

#include <builder/error.h>

#include <parser/type.h>
#include <parser/search.h>
#include <parser/literals.h>

#include <fmt/format.h>

Typename Builder::resolveTypename(const Node *node) {
    switch (node->is<Kind>()) {
        case Kind::NamedTypename: {
            auto e = node->as<NamedTypenameNode>();

            auto match = [e](const Node *node) {
                if (!node->is(Kind::Type))
                    return false;

                return node->as<TypeNode>()->name == e->name;
            };

            auto *type = search::exclusive::scope(node, match)->as<TypeNode>();

            if (!type)
                type = searchDependencies(match)->as<TypeNode>();

            if (auto alias = type->alias())
                return resolveTypename(alias);

            return NamedTypename { type };
        }

        case Kind::PrimitiveTypename:
            return PrimitiveTypename { node->as<PrimitiveTypenameNode>()->type };

        case Kind::OptionalTypename: {
            auto e = node->as<OptionalTypenameNode>();

            return OptionalTypename {
                std::make_shared<Typename>(resolveTypename(e->body())), e->bubbles
            };
        }

        case Kind::ReferenceTypename: {
            auto e = node->as<ReferenceTypenameNode>();

            return ReferenceTypename {
                std::make_shared<Typename>(resolveTypename(e->body())), e->isMutable
            };
        }

        case Kind::ArrayTypename: {
            auto e = node->as<ArrayTypenameNode>();

            return ArrayTypename {
                e->type,

                std::make_shared<Typename>(resolveTypename(e->body())),

                e->type == ArrayKind::FixedSize ? std::get<uint64_t>(e->fixedSize()->value) : 0
            };
        }

        default:
            throw VerifyError(node, "Expected typename node, but got something else.");
    }
}

Type *Builder::makePrimitiveType(PrimitiveType type) const {
    switch (type) {
        case PrimitiveType::Any: return Type::getInt64Ty(context);
        case PrimitiveType::Null: return Type::getInt8PtrTy(context);
        case PrimitiveType::Nothing: return Type::getVoidTy(context);
        case PrimitiveType::Bool: return Type::getInt1Ty(context);

        case PrimitiveType::Byte:
        case PrimitiveType::UByte:
            return Type::getInt8Ty(context);

        case PrimitiveType::Short:
        case PrimitiveType::UShort:
            return Type::getInt16Ty(context);

        case PrimitiveType::Int:
        case PrimitiveType::UInt:
            return Type::getInt32Ty(context);

        case PrimitiveType::Long:
        case PrimitiveType::ULong:
            return Type::getInt64Ty(context);

        case PrimitiveType::Float: return Type::getFloatTy(context);
        case PrimitiveType::Double: return Type::getDoubleTy(context);

        default: return nullptr;
    }
}

Type *Builder::makeTypename(const Typename &type) {
    struct {
        Builder &builder;

        Type *operator()(const PrimitiveTypename &type) const {
            return builder.makePrimitiveType(type.type);
        }

        Type *operator()(const NamedTypename &type) const {
            return builder.makeType(type.type)->type;
        }

        Type *operator()(const OptionalTypename &type) const {
            throw std::exception();
        }

        Type *operator()(const ReferenceTypename &type) const {
            return PointerType::get(builder.makeTypename(*type.value), 0);
        }

        Type *operator()(const ArrayTypename &type) const {
            switch (type.kind) {
                case ArrayKind::FixedSize:
                    return ArrayType::get(builder.makeTypename(*type.value), type.size);
                case ArrayKind::Unbounded:
                    return builder.makeTypename(*type.value);
                default:
                    throw std::runtime_error(fmt::format("Type {} is unimplemented.", toString(type)));
            }
        }

        Type *operator()(const FunctionTypename &type) const {
            throw std::exception();
        }
    } visitor { *this };

    return std::visit(visitor, type);
}
