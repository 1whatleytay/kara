#include <builder/handlers.h>

#include <builder/target.h>
#include <builder/builtins.h>

#include <parser/function.h>
#include <parser/literals.h>
#include <parser/root.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <cassert>

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

        auto wrappedResult = ops::matching::call(context, { typeNode->type }, {}, input);
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

        if (!(functions.empty() && unresolved.builtins.empty())) {
            return ops::matching::unwrap(
                ops::matching::call(context, functions, unresolved.builtins, input), unresolved.from);
        }

        // okay, we don't really have anything obvious to call so let's infer it before dying...
        // we might want to move this to another function but for now I'll keep it because it has `die()`
        auto infer = ops::makeInfer(context, unresolved);
        // this infer call ^^ banks on the fact that makeInfer on an unresolved
        // will just directly return the variable name without any extra calling
        // ^^ this is just bad code

        auto realType = findRealType(infer.type);
        assert(realType);

        auto functionType = std::get_if<utils::FunctionTypename>(realType);

        if (!functionType)
            die("Reference did not resolve to any functions to call.");

        auto real = ops::makeRealType(context, infer);
        auto llvmReal = ops::get(context, real); // ?

        return ops::matching::unwrap(ops::matching::call(context, *functionType, llvmReal, input), unresolved.from);
    }

    Maybe<builder::Wrapped> makeDotForField(
        const Context &context, const builder::Result &value, const parser::Reference *node) {
        // :| might generate duplicate code here, but pretty sure it generated duplicate code in last system too

        // Set up to check if property exists, dereference if needed
        auto [parent, subtype] = ops::findRealTypePair(value.type);

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

        auto structRef = ops::makeRealType(context, value);

        uint32_t flags = builder::Result::FlagReference | (value.flags & (builder::Result::FlagTemporary));

        if (parent) {
            auto refType = std::get_if<utils::ReferenceTypename>(parent);
            assert(refType);

            if (refType->isMutable)
                flags |= (builder::Result::FlagMutable);
        } else {
            flags |= (value.flags & (builder::Result::FlagMutable));
        }

        return builder::Result {
            flags,
            context.ir ? context.ir->CreateStructGEP(builderType->type, ops::ref(context, structRef), index, node->name)
                       : nullptr,
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

    bool makeInitializeNumber(const Context &context, llvm::Value *ptr, const utils::Typename &type) {
        auto prim = std::get_if<utils::PrimitiveTypename>(&type);

        if (!(prim && prim->isNumber()))
            return false;

        auto llvmType = context.builder.makePrimitiveType(prim->type);

        llvm::Value *value;

        if (prim->isFloat())
            value = llvm::ConstantFP::get(llvmType, 0);
        else
            value = llvm::ConstantInt::get(llvmType, 0);

        context.ir->CreateStore(value, ptr);

        return true;
    }

    bool makeInitializeReference(const Context &context, llvm::Value *ptr, const utils::Typename &type) {
        auto reference = std::get_if<utils::ReferenceTypename>(&type);

        if (!(reference && reference->kind != utils::ReferenceKind::Shared))
            return false;

        auto llvmType = context.builder.makeTypename(type);

        assert(llvmType->isPointerTy());

        auto llvmPointerType = reinterpret_cast<llvm::PointerType *>(llvmType);
        auto value = llvm::ConstantPointerNull::get(llvmPointerType);

        context.ir->CreateStore(value, ptr);

        return true;
    }

    bool makeInitializeVariableArray(const Context &context, llvm::Value *ptr, const utils::Typename &type) {
        auto array = std::get_if<utils::ArrayTypename>(&type);

        if (!(array && array->kind == utils::ArrayKind::VariableSize))
            return false;

        auto llvmType = context.builder.makeTypename(type);

        assert(llvmType->isStructTy());

        auto llvmStructType = reinterpret_cast<llvm::StructType *>(llvmType);

        auto pointerType = context.builder.makeTypename(*array->value);
        auto llvmPointerType = llvm::PointerType::get(pointerType, 0);

        auto zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 0);
        auto null = llvm::ConstantPointerNull::get(llvmPointerType);

        auto empty = llvm::ConstantStruct::ConstantStruct::get(llvmStructType, { zero, zero, null });

        context.ir->CreateStore(empty, ptr);

        return true;
    }

    bool makeInitializeStruct(const Context &context, llvm::Value *ptr, const utils::Typename &type) {
        auto named = std::get_if<utils::NamedTypename>(&type);

        if (!named)
            return false;

        auto llvmType = context.builder.makeTypename(type);
        auto size = context.builder.target.layout->getTypeAllocSize(llvmType);

        if (context.ir) {
            auto i8 = context.ir->getInt8Ty();
            auto zero = llvm::ConstantInt::get(i8, 0);

            context.ir->CreateMemSet(ptr, zero, size, llvm::MaybeAlign());
        }

        return true;
    }

    bool makeInitializeIgnore(const Context &context, llvm::Value *, const utils::Typename &type) { return true; }

    bool makeDestroyReference(const Context &context, llvm::Value *, const utils::Typename &type) {
        auto reference = std::get_if<utils::ReferenceTypename>(&type);

        // return true will mark reference as handled
        return reference && reference->kind == utils::ReferenceKind::Regular;
    }

    bool makeDestroyUnique(const Context &context, llvm::Value *ptr, const utils::Typename &type) {
        auto reference = std::get_if<utils::ReferenceTypename>(&type);

        if (!(reference && reference->kind == utils::ReferenceKind::Unique))
            return false;

        auto pointerType = context.builder.makeTypename(*reference);

        auto freeFunc = context.builder.getFree();
        auto dataType = llvm::Type::getInt8PtrTy(context.builder.context);
        auto value = context.ir->CreateLoad(pointerType, ptr);

        auto pointer = context.ir->CreatePointerCast(value, dataType);

        ops::makeDestroy(context, value, *reference->value);

        context.ir->CreateCall(freeFunc, { pointer });

        return true;
    }

    bool makeDestroyVariableArray(const Context &context, llvm::Value *ptr, const utils::Typename &type) {
        auto baseType = ops::findRealType(type);

        auto array = std::get_if<utils::ArrayTypename>(baseType);

        if (!(array && array->kind == utils::ArrayKind::VariableSize))
            return false;

        auto arrayStructType = context.builder.makeVariableArrayType(*array->value);
        auto elementType = context.builder.makeTypename(*array->value);
        auto elementPointer = llvm::PointerType::get(elementType, 0);

        // i regret assembling this a bit
        builder::Result decoy {
            builder::Result::FlagReference | builder::Result::FlagMutable, // ???
            ptr,
            type,
            nullptr,
        };

        auto arrayResult = ops::makeRealType(context, decoy);

        auto free = context.builder.getFree();
        auto dataType = llvm::Type::getInt8PtrTy(context.builder.context);

        auto dataPtr = context.ir->CreateStructGEP(arrayStructType, ops::ref(context, arrayResult), 2); // 2 is data
        auto dataPtrCasted = context.ir->CreatePointerCast(context.ir->CreateLoad(elementPointer, dataPtr), dataType);

        context.ir->CreateCall(free, { dataPtrCasted });

        // TODO: this is scary but make destroy has to loop over the elements in the array...
        //  ... and call ops::makeDestroy for each

        return true;
    }

    bool makeDestroyRegular(const Context &context, llvm::Value *ptr, const utils::Typename &type) {
        // Try to call destroy invocables... call will throw if options are empty
        auto destroyFunction = context.builder.lookupDestroy(type);

        if (destroyFunction) {
            // attempt to avoid this construction forces me to create it here
            auto parameter = builder::Result { builder::Result::FlagTemporary | builder::Result::FlagReference, ptr,
                type, nullptr };

            auto result = ops::matching::call(context, { destroyFunction }, { /* dwbi builtins */ },
                ops::matching::MatchInput { { parameter }, { /* no names */ } });

            assert(!std::holds_alternative<matching::CallError>(result));
        }

        if (auto named = std::get_if<utils::NamedTypename>(&type)) {
            auto containedType = named->type;
            auto builderType = context.builder.makeType(containedType);

            if (builderType->implicitDestructor) {
                auto func = builderType->implicitDestructor->function;

                // duplicate sanity check
                {
                    auto paramType = ptr->getType();
                    assert(paramType->isPointerTy());

                    auto elementType = paramType->getPointerElementType();
                    assert(elementType == builderType->type);
                }

                context.ir->CreateCall(func, { ptr });
            }
        }

        return true;
    }
}