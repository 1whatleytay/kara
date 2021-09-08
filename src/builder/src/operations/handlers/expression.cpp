#include <builder/handlers.h>

#include <builder/builtins.h>

#include <parser/function.h>
#include <parser/literals.h>
#include <parser/root.h>
#include <parser/type.h>
#include <parser/variable.h>

namespace kara::builder::ops::handlers {
    Maybe<builder::Wrapped> makeCallOnNew(
        const Context &context, const builder::Unresolved &unresolved, const matching::MatchInput &input) {
        auto isNewNode = [](const hermes::Node *n) { return n->is(parser::Kind::New); };
        auto newIt = std::find_if(unresolved.references.begin(), unresolved.references.end(), isNewNode);

        if (newIt == unresolved.references.end())
            return std::nullopt;

        auto newNode = (*newIt)->as<parser::New>();
        auto type = context.builder.resolveTypename(newNode->type());

        auto typeNode = std::get_if<utils::NamedTypename>(&type);

        if (!typeNode)
            die("New parameters may only be passed to a type/struct.");

        auto wrappedResult = ops::matching::call(context, { typeNode->type }, { }, input);
        auto returnResult = ops::matching::unwrap(wrappedResult, unresolved.from);

        auto output = ops::nouns::makeNew(context, type);

        if (context.ir)
            context.ir->CreateStore(ops::get(context, returnResult), ops::get(context, output));

        return output;
    }

    Maybe<builder::Wrapped> makeCallOnFunctionOrType(
        const Context &context, const builder::Unresolved &unresolved, const matching::MatchInput &input) {
        std::vector<const hermes::Node *> functions;

        auto isFunctionOrType
            = [](const hermes::Node *n) { return n->is(parser::Kind::Function) || n->is(parser::Kind::Type); };

        std::copy_if(unresolved.references.begin(), unresolved.references.end(), std::back_inserter(functions),
            isFunctionOrType);

        if (functions.empty() && unresolved.builtins.empty())
            die("Reference did not resolve to any functions to call.");

        return ops::matching::unwrap(
            ops::matching::call(context, functions, unresolved.builtins, input),
            unresolved.from);
    }

    // moved to builtin solution, that would allow parameters to be taken and work the same as fields
//    Maybe<builder::Wrapped> makeDotForArrayProperties(
//        const Context &context, const builder::Result &value, const parser::Reference *node) {
//
//    }

    Maybe<builder::Wrapped> makeDotForField(
        const Context &context, const builder::Result &value, const parser::Reference *node) {
        // :| might generate duplicate code here, but pretty sure it generated duplicate code in last system too

        // Set up to check if property exists, dereference if needed
        auto subtype = ops::findReal(value.type);

        auto type = std::get_if<utils::NamedTypename>(subtype);
        if (!type)
            return std::nullopt;

        builder::Type *builderType = context.builder.makeType(type->type);

        auto match = [node](auto var) { return var->name == node->name; };

        auto fields = type->type->fields();
        auto iterator = std::find_if(fields.begin(), fields.end(), match);

        if (iterator == fields.end())
            return std::nullopt;

        auto *varNode = *iterator;

        if (!varNode->hasFixedType)
            throw VerifyError(varNode, "All struct variables must have fixed type.");

        size_t index = builderType->indices.at(varNode);

        auto structRef = ops::makeReal(context, value);

        return builder::Result {
            (value.flags & (builder::Result::FlagMutable | builder::Result::FlagTemporary))
                | builder::Result::FlagReference,
            context.ir ? context.ir->CreateStructGEP(ops::ref(context, structRef), index, node->name) : nullptr,
            context.builder.resolveTypename(varNode->fixedType()),
            context.accumulator,
        };
    }

    Maybe<builder::Wrapped> makeDotForUFCS(
        const Context &context, const builder::Result &value, const parser::Reference *node) {
        const auto &global = context.builder.root->children;

        auto matchFunction = [&name = node->name](const hermes::Node *node) {
            if (!node->is(parser::Kind::Function))
                return false;

            auto *e = node->as<parser::Function>();
            if (e->name != name || e->parameterCount == 0)
                return false;

            return true;
        };

        auto n = context.builder.searchAllDependencies(matchFunction);

        auto builtins = ops::handlers::builtins::matching(node->name);

        // last in row of makeDot
        if (n.empty() && builtins.empty())
            die("Could not find method or field with name {}.", node->name);

        return builder::Unresolved {
            node,
            n,
            builtins,
            std::make_unique<builder::Result>(value),
        };
    }

    bool makeDestroyReference(const Context &context, const builder::Result &result) {
        auto reference = std::get_if<utils::ReferenceTypename>(&result.type);

        // return true will mark reference as handled
        return reference && reference->kind == utils::ReferenceKind::Regular;
    }

    bool makeDestroyUnique(const Context &context, const builder::Result &result) {
        auto reference = std::get_if<utils::ReferenceTypename>(&result.type);

        if (!(reference && reference->kind == utils::ReferenceKind::Unique))
            return false;

        auto free = context.builder.getFree();
        auto dataType = llvm::Type::getInt8PtrTy(context.builder.context);
        auto pointer = context.ir->CreatePointerCast(ops::get(context, result), dataType);

        // alternative is keep Kind::Reference but only CreateLoad(result.value)
        auto containedValue = builder::Result {
            builder::Result::FlagTemporary | builder::Result::FlagReference, ops::get(context, result),
            *reference->value,
            nullptr, // don't do it!! &accumulator would be here but I want raw
        };

        ops::makeDestroy(context, containedValue);

        context.ir->CreateCall(free, { pointer });

        return true;
    }

    bool makeDestroyGlobal(const Context &context, const builder::Result &result) {
        // Try to call destroy invocables... call will throw if options are empty
        if (!context.builder.destroyInvocables.empty())
            ops::matching::call(
                context,
                context.builder.destroyInvocables,
                { /* dwbi builtins */ },
                ops::matching::MatchInput { { result }, {} });

        if (auto named = std::get_if<utils::NamedTypename>(&result.type)) {
            auto containedType = named->type;
            auto builderType = context.builder.makeType(containedType);

            auto func = builderType->implicitDestructor->function;

            llvm::Value *param = ops::ref(context, result);

            // duplicate sanity check
            {
                auto paramType = param->getType();
                assert(paramType->isPointerTy());

                auto elementType = paramType->getPointerElementType();
                assert(elementType == builderType->type);
            }

            context.ir->CreateCall(func, { param });
        }

        return true;
    }
}