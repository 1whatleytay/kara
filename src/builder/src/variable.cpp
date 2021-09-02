#include <builder/builder.h>

#include <builder/error.h>
#include <builder/operations.h>

#include <parser/expression.h>
#include <parser/literals.h>
#include <parser/variable.h>

#include <fmt/format.h>

namespace kara::builder {
    Variable::Variable(const parser::Variable *node, builder::Builder &builder)
        : node(node) {
        if (node->isMutable)
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

        value = new llvm::GlobalVariable(*builder.module, builder.makeTypename(type), node->isMutable,
            node->isExternal ? L::ExternalLinkage : L::PrivateLinkage, defaultValue, node->name);
    }

    Variable::Variable(const parser::Variable *node, builder::Scope &scope)
        : node(node) {
        assert(scope.function);

        builder::Function &function = *scope.function;

        assert(node->hasFixedType || node->value());

        std::optional<builder::Result> possibleDefault;

        auto context = ops::Context::from(scope);

        if (node->hasInitialValue) {
            builder::Result result = ops::expression::makeExpression(context, node->value());

            if (node->hasFixedType) {
                auto fixedType = function.builder.resolveTypename(node->fixedType());

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
            type = function.builder.resolveTypename(node->fixedType());
        }

        if (scope.current) {
            value = ops::makeAlloca(context, type, node->name);

            if (possibleDefault)
                scope.current->CreateStore(ops::get(context, *possibleDefault), value);
        }
    }

    Variable::Variable(const parser::Variable *node, llvm::Value *input, builder::Scope &scope)
        : node(node) {
        assert(scope.function);

        builder::Function &function = *scope.function;

        auto context = ops::Context::from(scope);

        if (!node->hasFixedType || node->value())
            throw VerifyError(node,
                "A function parameter must have fixed type and no "
                "default value, unimplemented.");

        type = function.builder.resolveTypename(node->fixedType());

        if (scope.current) {
            value = ops::makeAlloca(context, type, fmt::format("{}_value", node->name));
            function.entry.CreateStore(input, value);
        }
    }
}
