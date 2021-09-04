#include <builder/handlers.h>

#include <parser/root.h>
#include <parser/type.h>
#include <parser/literals.h>
#include <parser/variable.h>
#include <parser/function.h>

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

        auto wrappedResult = ops::matching::call(context, { typeNode->type }, input);
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

        std::copy_if(
            unresolved.references.begin(),
            unresolved.references.end(),
            std::back_inserter(functions),
            isFunctionOrType);

        if (functions.empty())
            die("Reference did not resolve to any functions to call.");

        return ops::matching::unwrap(ops::matching::call(context, functions, input), unresolved.from);
    }

    std::pair<size_t, const utils::Typename *> sourceType(const utils::Typename &type) {
        const utils::Typename *subtype = &type;

        size_t numReferences = 0;

        while (auto *sub = std::get_if<utils::ReferenceTypename>(subtype)) {
            subtype = sub->value.get();
            numReferences++;
        }

        return { numReferences, subtype };
    }

    llvm::Value *refToSourceType(const Context &context, const builder::Result &value, size_t num) {
        llvm::Value *refTo = num > 0 ? ops::get(context, value) : ops::ref(context, value);

        if (context.ir) {
            for (size_t a = 1; a < num; a++) {
                refTo = context.ir->CreateLoad(refTo);
            }
        }

        return refTo;
    }

    Maybe<builder::Wrapped> makeDotForArraySize(
        const Context &context, const builder::Result &value, const parser::Reference *node) {
        auto [count, subtype] = sourceType(value.type);

        auto array = std::get_if<utils::ArrayTypename>(subtype);
        if (!(node->name == "size" && array))
            return std::nullopt;

        switch (array->kind) {
        case utils::ArrayKind::UnboundedSized: {
            auto expression = array->expression;

            if (!context.cache)
                die("Cache required to size field on array.");

            auto cached = context.cache->find(&Cache::expressions, expression);

            if (!cached)
                die("Attempting to access size of {} but size has not yet been calculated.", toString(value.type));

            return *cached; // may need concert to long
        }

        case utils::ArrayKind::FixedSize:
            return ops::nouns::makeNumber(context, array->size); // uh oh

        case utils::ArrayKind::Unbounded:
            return std::nullopt; // let UFCS maybe take action

        case utils::ArrayKind::Iterable:
        case utils::ArrayKind::VariableSize:
            throw;

        default:
            throw;
        }
    }

    Maybe<builder::Wrapped> makeDotForField(
        const Context &context, const builder::Result &value, const parser::Reference *node) {
        // :| might generate duplicate code here, but pretty sure it generated duplicate code in last system too

        // Set up to check if property exists, dereference if needed
        auto [count, subtype] = sourceType(value.type);

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

        auto structRef = refToSourceType(context, value, count);

        return builder::Result {
            (value.flags & (builder::Result::FlagMutable | builder::Result::FlagTemporary))
                | builder::Result::FlagReference,
            context.ir ? context.ir->CreateStructGEP(structRef, index, node->name) : nullptr,
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

            // last in row of makeDot
            if (n.empty())
                die("Could not find method or field with name {}.", node->name);

            return builder::Unresolved {
                node,
                n,
                std::make_unique<builder::Result>(value)
            };
    }
}