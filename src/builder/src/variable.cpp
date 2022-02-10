#include <builder/builder.h>

#include <builder/error.h>
#include <builder/operations.h>

#include <parser/expression.h>
#include <parser/literals.h>
#include <parser/variable.h>

#include <fmt/format.h>

#include <cassert>

namespace kara::builder {
    Variable::Variable(const parser::Variable *node, builder::Builder &builder)
        : node(node) {
        if (node->isMutable && !builder.options.mutableGlobals)
            throw VerifyError(node, "Global variables cannot be mutable.");

        if (!node->hasFixedType)
            throw VerifyError(node, "Global variables must have a fixed type.");

        if (node->value())
            throw VerifyError(node, "Global variables cannot be initialized.");

        type = builder.resolveTypename(node->fixedType());

        using L = llvm::GlobalVariable::LinkageTypes;

        assert(!node->hasInitialValue);

        llvm::Constant *defaultValue = nullptr;

        if (node->hasConstantValue) {
            assert(node->hasFixedType);

            auto numberNode = node->constantValue();

            llvm::Type *resolvedType
                = node->hasFixedType ? builder.makeTypename(builder.resolveTypename(node->fixedType())) : nullptr;

            struct {
                builder::Builder &builder;
                llvm::Type *resolvedType;

                llvm::Constant *operator()(uint64_t v) {
                    return llvm::ConstantInt::get(
                        resolvedType ? resolvedType : llvm::Type::getInt64Ty(builder.context), v);
                }

                llvm::Constant *operator()(int64_t v) {
                    return llvm::ConstantInt::getSigned(
                        resolvedType ? resolvedType : llvm::Type::getInt64Ty(builder.context), v);
                }

                llvm::Constant *operator()(double v) {
                    return llvm::ConstantFP::get(
                        resolvedType ? resolvedType : llvm::Type::getDoubleTy(builder.context), v);
                }
            } visitor { builder, resolvedType };

            defaultValue = std::visit(visitor, numberNode->value);
        }

        if (!node->isExternal && !defaultValue) {
            throw VerifyError(node, "All constant globals must be initialized with some value.");
        }

        value = new llvm::GlobalVariable(*builder.module, builder.makeTypename(type), !node->isMutable,
            node->isExternal ? L::ExternalLinkage : L::WeakAnyLinkage, defaultValue, node->name);
    }

    Variable::Variable(const parser::Variable *node, const ops::Context &context)
        : node(node) {
        assert(context.function);

        assert(node->hasFixedType || node->value());

        std::optional<builder::Result> possibleDefault;

        if (node->hasInitialValue) {
            builder::Result result = ops::expression::make(context, node->value());

            if (node->hasFixedType) {
                auto fixedType = context.function->builder.resolveTypename(node->fixedType());

                std::optional<builder::Result> resultConverted = ops::makeConvert(context, result, fixedType);

                if (!resultConverted) {
                    throw VerifyError(node->value(), "Cannot convert from type {} to variable fixed type {}.",
                        toString(result.type), toString(fixedType));
                }

                result = *resultConverted;
            }

            result = ops::makePass(context, result);

            type = result.type;
            possibleDefault = result; // copy :|
        } else {
            type = context.function->builder.resolveTypename(node->fixedType());
        }

        if (context.ir) {
            value = ops::makeAlloca(context, type, node->name);

            if (possibleDefault)
                context.ir->CreateStore(ops::get(context, *possibleDefault), value);
            else
                ops::makeInitialize(context, value, type);
        }
    }

    Variable::Variable(const parser::Variable *node, const ops::Context &context, llvm::Value *argument)
        : node(node) {
        assert(context.function);

        if (!node->hasFixedType || node->value())
            throw VerifyError(node,
                "A function parameter must have fixed type and no "
                "default value, unimplemented.");

        type = context.builder.resolveTypename(node->fixedType());

        // THIS IS BAD, its probably going to generate in scope, function needs to create IRBuilder for entry?
        if (context.ir) {
            value = ops::makeAlloca(context, type, fmt::format("{}_value", node->name));
            context.function->entry.CreateStore(argument, value);
        }
    }
}
