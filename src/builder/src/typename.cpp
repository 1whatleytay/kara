#include <builder/builder.h>

#include <builder/error.h>

#include <parser/literals.h>
#include <parser/search.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <fmt/format.h>

#include <cassert>

namespace kara::builder {
    utils::Typename Builder::resolveTypename(const hermes::Node *node) {
        switch (node->is<parser::Kind>()) {
        case parser::Kind::NamedTypename: {
            auto e = node->as<parser::NamedTypename>();

            auto match = [e](const hermes::Node *node) {
                if (!node->is(parser::Kind::Type))
                    return false;

                return node->as<parser::Type>()->name == e->name;
            };

            auto *type = parser::search::exclusive::scope(node, match)->as<parser::Type>();

            if (!type)
                type = searchDependencies(match)->as<parser::Type>();

            if (!type)
                throw VerifyError(node, "Cannot find type {}.", e->name);

            if (auto alias = type->alias())
                return resolveTypename(alias);

            return utils::NamedTypename { type->name, type };
        }

        case parser::Kind::PrimitiveTypename:
            return utils::PrimitiveTypename { node->as<parser::PrimitiveTypename>()->type };

        case parser::Kind::OptionalTypename: {
            auto e = node->as<parser::OptionalTypename>();

            return utils::OptionalTypename { std::make_shared<utils::Typename>(resolveTypename(e->body())),
                e->bubbles };
        }

        case parser::Kind::ReferenceTypename: {
            auto e = node->as<parser::ReferenceTypename>();

            assert(!e->isShared);

            if (e->isCPointer) {
                assert(e->kind == utils::ReferenceKind::Regular);

                return utils::ReferenceTypename {
                    std::make_shared<utils::Typename>(utils::ArrayTypename {
                        utils::ArrayKind::Unbounded,
                        std::make_shared<utils::Typename>(resolveTypename(e->body()))
                    }),
                    e->isMutable.value_or(true),
                    e->kind,
                };
            } else {
                return utils::ReferenceTypename {
                    std::make_shared<utils::Typename>(resolveTypename(e->body())),
                    e->isMutable.value_or(e->kind != utils::ReferenceKind::Regular),
                    e->kind,
                };
            }
        }

        case parser::Kind::ArrayTypename: {
            auto e = node->as<parser::ArrayTypename>();

            struct {
                uint64_t operator()(uint64_t v) { return v; }
                uint64_t operator()(int64_t v) {
                    assert(v >= 0);
                    return v;
                }
                uint64_t operator()(double v) { throw; }
            } visitor;

            return utils::ArrayTypename {
                e->type,

                std::make_shared<utils::Typename>(resolveTypename(e->body())),

                e->type == utils::ArrayKind::FixedSize ? std::visit(visitor, e->fixedSize()->value) : 0,
                e->type == utils::ArrayKind::UnboundedSized ? e->variableSize() : nullptr,
            };
        }

        case parser::Kind::FunctionTypename: {
            auto e = node->as<parser::FunctionTypename>();

            auto parameters = e->parameters();
            utils::FunctionParameters paramResult(parameters.size());

            std::transform(parameters.begin(), parameters.end(), paramResult.begin(), [this](const hermes::Node *p) {
                // this is ass, where are variant types?? - taylor
                switch (p->is<parser::Kind>()) {
                case parser::Kind::Variable: {
                    auto e = p->as<parser::Variable>();

                    assert(e->hasFixedType);

                    return std::make_pair(e->name, resolveTypename(e->fixedType()));
                }

                default:
                    return std::make_pair(std::string(), resolveTypename(p));
                }
            });
            utils::Typename returnResult = resolveTypename(e->returnType());

            return utils::FunctionTypename {
                e->kind,
                std::move(paramResult),
                std::make_shared<utils::Typename>(std::move(returnResult)),
                e->isLocked,
            };
        }

        default:
            throw;
        }
    }

    llvm::Type *Builder::makePrimitiveType(utils::PrimitiveType type) const {
        switch (type) {
        case utils::PrimitiveType::Any:
            return llvm::Type::getInt64Ty(context);
        case utils::PrimitiveType::Null:
            return llvm::Type::getInt8PtrTy(context);
        case utils::PrimitiveType::Nothing:
            return llvm::Type::getVoidTy(context);
        case utils::PrimitiveType::Bool:
            return llvm::Type::getInt1Ty(context);

        case utils::PrimitiveType::Byte:
        case utils::PrimitiveType::UByte:
            return llvm::Type::getInt8Ty(context);

        case utils::PrimitiveType::Short:
        case utils::PrimitiveType::UShort:
            return llvm::Type::getInt16Ty(context);

        case utils::PrimitiveType::Int:
        case utils::PrimitiveType::UInt:
            return llvm::Type::getInt32Ty(context);

        case utils::PrimitiveType::Long:
        case utils::PrimitiveType::ULong:
            return llvm::Type::getInt64Ty(context);

        case utils::PrimitiveType::Float:
            return llvm::Type::getFloatTy(context);
        case utils::PrimitiveType::Double:
            return llvm::Type::getDoubleTy(context);

        default:
            return nullptr;
        }
    }

    llvm::Type *Builder::makeTypename(const utils::Typename &type) {
        struct {
            builder::Builder &builder;

            llvm::Type *operator()(const utils::PrimitiveTypename &type) const {
                return builder.makePrimitiveType(type.type);
            }

            llvm::Type *operator()(const utils::NamedTypename &type) const { return builder.makeType(type.type)->type; }

            llvm::Type *operator()(const utils::OptionalTypename &type) const {
                return builder.makeOptionalType(*type.value);
            }

            llvm::Type *operator()(const utils::ReferenceTypename &type) const {
                return llvm::PointerType::get(builder.makeTypename(*type.value), 0);
            }

            llvm::Type *operator()(const utils::ArrayTypename &type) const {
                switch (type.kind) {
                case utils::ArrayKind::FixedSize:
                    return llvm::ArrayType::get(builder.makeTypename(*type.value), type.size);
                case utils::ArrayKind::Unbounded:
                case utils::ArrayKind::UnboundedSized: // some thinking probably needs to
                    // be done here too
                    return builder.makeTypename(*type.value);
                case utils::ArrayKind::VariableSize:
                    return builder.makeVariableArrayType(*type.value);
                default:
                    throw std::runtime_error(fmt::format("Type {} is unimplemented.", toString(type)));
                }
            }

            llvm::Type *operator()(const utils::FunctionTypename &type) const {
                if (type.kind != utils::FunctionKind::Pointer)
                    throw std::runtime_error("Function typename must be pointer type.");

                auto &paramIn = type.parameters;

                std::vector<std::pair<std::string, llvm::Type *>> parameters(type.parameters.size());

                for (size_t a = 0; a < paramIn.size(); a++) {
                    parameters[a] = { fmt::format("_{}", a), builder.makeTypename(paramIn[a].second) };
                }

                auto returnType = builder.makeTypename(*type.returnType);

                auto formattedResult = builder.platform->formatArguments(builder.target, { returnType, parameters });

                auto functionType = llvm::FunctionType::get(
                    formattedResult.returnType, formattedResult.parameterTypes(), false);

                return llvm::PointerType::get(functionType, 0);
            }
        } visitor { *this };

        return std::visit(visitor, type);
    }
}
