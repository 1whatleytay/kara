#include <builder/builder.h>

#include <builder/error.h>

#include <parser/literals.h>
#include <parser/variable.h>

#include <fmt/format.h>

BuilderVariable::BuilderVariable(const VariableNode *node, Builder &builder) : node(node) {
//    if (node->isMutable)
//        throw VerifyError(node, "Global variables cannot be mutable.");

    if (!node->hasFixedType)
        throw VerifyError(node, "Global variables must have a fixed type.");

    if (node->value())
        throw VerifyError(node, "Global variables cannot be initialized.");

    type = builder.resolveTypename(node->fixedType());

    using L = GlobalVariable::LinkageTypes;

    assert(!node->hasInitialValue);

    Constant *defaultValue = nullptr;

    if (node->hasConstantValue) {
        assert(node->hasFixedType);

        auto numberNode = node->constantValue();

        Type *resolvedType = node->hasFixedType
            ? builder.makeTypename(builder.resolveTypename(node->fixedType())) : nullptr;

        struct {
            Builder &builder;
            Type *resolvedType;

            Constant *operator()(uint64_t v) {
                return ConstantInt::get(resolvedType ? resolvedType : Type::getInt64Ty(builder.context), v);
            }

            Constant *operator()(int64_t v) {
                return ConstantInt::getSigned(resolvedType ? resolvedType : Type::getInt64Ty(builder.context), v);
            }

            Constant *operator()(double v) {
                return ConstantFP::get(resolvedType ? resolvedType : Type::getDoubleTy(builder.context), v);
            }
        } visitor { builder, resolvedType };

        defaultValue = std::visit(visitor, numberNode->value);
    }

    value = new GlobalVariable(
        *builder.module, builder.makeTypename(type), node->isMutable,
        node->isExternal ? L::ExternalLinkage : L::PrivateLinkage, defaultValue, node->name);
}

BuilderVariable::BuilderVariable(const VariableNode *node, BuilderScope &scope) : node(node) {
    assert(scope.function);

    BuilderFunction &function = *scope.function;

    assert(node->hasFixedType || node->value());

    std::optional<BuilderResult> possibleDefault;

    if (node->hasInitialValue) {
        BuilderResult result = scope.makeExpression(node->value());

        if (node->hasFixedType) {
            auto fixedType = function.builder.resolveTypename(node->fixedType());

            std::optional<BuilderResult> resultConverted = scope.convert(result, fixedType);

            if (!resultConverted) {
                throw VerifyError(node->value(),
                    "Cannot convert from type {} to variable fixed type {}.",
                    toString(result.type),
                    toString(fixedType));
            }

            result = *resultConverted;
        }

        result = scope.pass(result);

        type = result.type;
        possibleDefault = result; // copy :|
    } else {
        type = function.builder.resolveTypename(node->fixedType());
    }

    if (scope.current) {
        value = function.entry.CreateAlloca(function.builder.makeTypename(type), nullptr, node->name);

        if (possibleDefault)
            scope.current->CreateStore(scope.get(*possibleDefault), value);
    }
}

BuilderVariable::BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope) : node(node) {
    assert(scope.function);

    BuilderFunction &function = *scope.function;

    if (!node->hasFixedType || node->value())
        throw VerifyError(node, "A function parameter must have fixed type and no default value, unimplemented.");

    type = function.builder.resolveTypename(node->fixedType());

    if (scope.current) {
        value = function.entry.CreateAlloca(
            function.builder.makeTypename(type), nullptr, fmt::format("{}_value", node->name));
        function.entry.CreateStore(input, value);
    }
}
