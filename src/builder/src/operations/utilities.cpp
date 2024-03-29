#include <builder/operations.h>

#include <builder/target.h>
#include <builder/manager.h>
#include <builder/handlers.h>

#include <parser/literals.h>
#include <parser/variable.h>

#include <cassert>

namespace kara::builder::ops {
    Context Context::noIR() const { return move(nullptr); }

    Context Context::move(llvm::IRBuilder<> *n) const {
        Context context = *this;

        context.ir = n;

        return context;
    }

    llvm::Value *get(const Context &context, const builder::Result &result) {
        if (result.isSet(builder::Result::FlagReference)) {
            if (context.ir) {
                auto sourceType = context.builder.makeTypename(result.type);
                return context.ir->CreateLoad(sourceType, result.value);
            }

            return nullptr;
        }

        return result.value;
    }

    llvm::Value *ref(const Context &context, const builder::Result &result) {
        if (result.isSet(builder::Result::FlagReference)) {
            return result.value;
        }

        if (context.ir) {
            assert(context.function);

            auto llvmType = context.function->builder.makeTypename(result.type);
            llvm::Value *ref = context.function->entry.CreateAlloca(llvmType); // maybe makeAlloca?

            context.ir->CreateStore(result.value, ref);

            return ref;
        } else {
            return nullptr;
        }
    }

    std::optional<utils::Typename> negotiate(const utils::Typename &left, const utils::Typename &right) {
        return handlers::resolve(
            std::array {
                handlers::negotiateEqual,
                handlers::negotiatePrimitive,
                handlers::negotiateReferenceAndNull,
            },
            left, right);
    }

    llvm::Value *makeAlloca(const Context &context, const utils::Typename &type, const std::string &name) {
        assert(context.function);

        if (auto array = std::get_if<utils::ArrayTypename>(&type)) {
            if (array->kind == utils::ArrayKind::Unbounded)
                throw std::runtime_error(fmt::format("Attempt to allocate type {} on stack.", toString(type)));

            if (array->kind == utils::ArrayKind::UnboundedSized)
                throw std::runtime_error(
                    fmt::format("VLA unsupported for type {0}. Use *{0} for allocation instead.", toString(type)));
        }

        return context.function->entry.CreateAlloca(context.builder.makeTypename(type), nullptr, name);
    }

    llvm::Value *makeMalloc(const Context &context, const utils::Typename &type, const std::string &name) {
        llvm::Value *arraySize = nullptr;

        if (auto array = std::get_if<utils::ArrayTypename>(&type)) {
            if (array->kind == utils::ArrayKind::Unbounded)
                throw std::runtime_error(fmt::format("Attempt to allocate type {} on heap.", toString(type)));

            if (array->kind == utils::ArrayKind::UnboundedSized) {
                assert(array->expression && context.cache);

                auto it = context.cache->find(&Cache::expressions, array->expression);
                if (it) {
                    arraySize = ops::get(context, *it);
                } else {
                    auto length = ops::expression::make(context, array->expression);
                    auto ulongTypename = utils::PrimitiveTypename { utils::PrimitiveType::ULong };

                    auto converted = ops::makeConvert(context, length, ulongTypename);

                    if (!converted) {
                        die("Expression cannot be converted to ulong for size for array.");
                    }

                    auto &result = *converted;

                    context.cache->expressions.insert({
                        array->expression,
                        std::make_unique<builder::Result>(result), // guaranteed to be ulong?
                    });

                    arraySize = ops::get(context, result);
                }

                // TODO: needs recursive implementation of sizes to account for
                // [[int:50]:50] ^ probably would be done in the great refactor
                // i dont think it will

                auto llvmElementType = context.builder.makeTypename(*array->value);
                size_t elementSize = context.builder.target.layout->getTypeStoreSize(llvmElementType);

                auto sizeType = llvm::Type::getInt64Ty(context.builder.context);

                llvm::Constant *llvmElementSize = llvm::ConstantInt::get(sizeType, elementSize);

                if (context.ir)
                    arraySize = context.ir->CreateMul(llvmElementSize, arraySize);
            }
        }

        if (!context.ir)
            return nullptr;

        auto llvmType = context.builder.makeTypename(type);
        auto pointerType = llvm::PointerType::get(llvmType, 0);

        size_t bytes = context.builder.target.layout->getTypeStoreSize(llvmType);
        auto malloc = context.builder.getMalloc();

        // if statement above can adjust size...
        if (!arraySize) {
            arraySize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), bytes);
        }

        return context.ir->CreatePointerCast(context.ir->CreateCall(malloc, { arraySize }), pointerType, name);
    }

    // remove from statement scope or call move operator
    builder::Result makePass(const Context &context, const Result &result) {
        auto array = std::get_if<utils::ArrayTypename>(&result.type);
        auto reference = std::get_if<utils::ReferenceTypename>(&result.type);

        auto isTemporary = result.isSet(builder::Result::FlagTemporary);
        auto isRegularReference = reference && reference->kind == utils::ReferenceKind::Regular;

        if (isTemporary) {
            if (context.accumulator && !isRegularReference) {
                context.accumulator->avoidDestroy.insert(result.uid);
            }
        } else {
            if ((reference && !isRegularReference)
                || (array && array->kind == utils::ArrayKind::VariableSize)) { // unique or shared and not temporary
                throw std::runtime_error(fmt::format(
                    "Passing non-temporary of type {} is prohibited. May require a move or copy.",
                    toString(result.type)));
            }
        }

        return result;
    }

    builder::Result makeInfer(const Context &context, const Wrapped &result) {
        struct {
            const Context &context;

            builder::Result operator()(const builder::Result &result) {
                if (!result.isSet(builder::Result::FlagExplicit)) {
                    auto func = std::get_if<utils::FunctionTypename>(&result.type);

                    if (func && !func->isLocked) {
                        ops::matching::MatchInput input; // no parameters
                        auto wrapper = ops::matching::call(context, *func, ops::get(context, result), input);
                        return ops::matching::unwrap(wrapper, nullptr);
                    }
                }

                return result;
            }

            builder::Result operator()(const builder::Unresolved &result) {
                // First variable in scope search will be lowest in scope.
                auto isVariable = [](const hermes::Node *node) { return node->is(parser::Kind::Variable); };
                auto varIterator = std::find_if(result.references.begin(), result.references.end(), isVariable);

                if (varIterator != result.references.end()) {
                    auto var = (*varIterator)->as<parser::Variable>();

                    builder::Variable *info;

                    if (var->parent->is(parser::Kind::Root)) {
                        info = context.builder.makeGlobal(var); // AH THIS WONT WORK FOR EXTERNAL
                    } else {
                        assert(context.cache);

                        info = context.cache->find(&Cache::variables, var);
                    }

                    if (!info)
                        die("Cannot find variable reference.");

                    // Why makeInfer here?
                    // The idea is that I can have a variable (y func () nothing)
                    // And then I can type `y` and have it call using the code in makeInfer.
                    // this doesn't work cuz now if you do &z it will evaluate z right away?
                    // future taylor: moving this double make infer to ops::expression::make
                    return builder::Result {
                        builder::Result::FlagReference | (info->node->isMutable ? builder::Result::FlagMutable : 0),
                        info->value,
                        info->type,
                        context.accumulator,
                    };
                }

                auto isNew = [](const hermes::Node *node) { return node->is(parser::Kind::New); };
                auto newIterator = std::find_if(result.references.begin(), result.references.end(), isNew);

                if (newIterator != result.references.end()) {
                    auto type = context.builder.resolveTypename((*newIterator)->as<parser::New>()->type());
                    return ops::nouns::makeNew(context, type);
                }

                std::vector<const hermes::Node *> functions;

                auto callable
                    = [](const hermes::Node *n) { return n->is(parser::Kind::Function) || n->is(parser::Kind::Type); };

                std::copy_if(
                    result.references.begin(), result.references.end(), std::back_inserter(functions), callable);

                if (!(functions.empty() && result.builtins.empty())) {
                    std::vector<builder::Result> params;

                    if (result.implicit)
                        params.push_back(*result.implicit);

                    auto returnResult = ops::matching::call(context, functions, result.builtins, { params, {} });
                    return ops::matching::unwrap(returnResult, result.from);
                } else {
                    throw VerifyError(result.from, "Reference does not implicitly resolve to anything.");
                }

                // if it is a variable, return first
                // if it is a function, find one with 0 params or infer params, e.g. some
                // version of match is needed
            }
        } visitor { context };

        return std::visit(visitor, result);
    }

    ParentChildTypePair findRealTypePair(const utils::Typename &type) {
        const utils::Typename *parent = nullptr;
        const utils::Typename *subtype = &type;

        while (auto *sub = std::get_if<utils::ReferenceTypename>(subtype)) {
            parent = subtype;
            subtype = sub->value.get();
        }

        return { parent, subtype };
    }

    const utils::Typename *findRealType(const utils::Typename &type) {
        auto [parent, subtype] = findRealTypePair(type);

        return subtype;
    }

    builder::Result makeRealType(const Context &context, const builder::Result &result) {
        builder::Result value = result;

        while (auto *r = std::get_if<utils::ReferenceTypename>(&value.type)) {
            value.flags &= ~(builder::Result::FlagMutable);
            value.flags |= r->isMutable ? builder::Result::FlagMutable : 0;
            value.flags |= builder::Result::FlagReference;

            if (value.isSet(builder::Result::FlagReference)) {
                if (context.ir) {
                    auto valueType = context.builder.makeTypename(value.type);
                    value.value = context.ir->CreateLoad(valueType, value.value);
                } else {
                    value.value = nullptr;
                }
            }

            value.type = *r->value;
        }

        return value;
    }

    std::optional<builder::Result> makeConvert(
        const Context &context, const builder::Result &value, const utils::Typename &type, bool force) {
        auto bridged = handlers::bridge(
            [&](const builder::Result &v) {
                return handlers::resolve(
                    std::array {
                        handlers::makeConvertBridgeImplicitReference,
                        handlers::makeConvertBridgeImplicitDereference,
                    },
                    context, v, type, force);
            },
            value);

        return handlers::resolve(
            std::array {
                handlers::makeConvertEqual,
                handlers::makeConvertForcedRefToRef,
                handlers::makeConvertForcedULongToRef,
                handlers::makeConvertForcedRefToULong,
                handlers::makeConvertForcedIntToBool,
                handlers::makeConvertForcedFuncPtrToFuncPtr,
                handlers::makeConvertUniqueOrMutableToRef,
                handlers::makeConvertUniqueToVariableArray,
                handlers::makeConvertExprArrayToUnboundedRef,
                handlers::makeConvertRefToAnyRef,
                handlers::makeConvertRefToUnboundedRef,
                handlers::makeConvertFixedRefToUnboundedRef,
                handlers::makeConvertNullToRef,
                handlers::makeConvertNullToOptional,
                handlers::makeConvertRefToBool,
                handlers::makeConvertOptionalToBool,
                handlers::makeConvertTypeToOptional,
                handlers::makeConvertIntToFloat,
                handlers::makeConvertFloatToInt,
                handlers::makeConvertPrimitiveExtend,
            },
            context, bridged, type, force);
    }

    std::optional<std::pair<Result, Result>> makeConvertExplicit(
        const Context &aContext, const Result &a, const Context &bContext, const Result &b) {
        std::optional<utils::Typename> mediator = negotiate(a.type, b.type);

        if (!mediator)
            return std::nullopt;

        auto left = ops::makeConvert(aContext, a, *mediator);
        auto right = ops::makeConvert(bContext, b, *mediator);

        if (!left || !right)
            return std::nullopt;

        return std::make_pair(*left, *right);
    }

    std::optional<std::pair<Result, Result>> makeConvertDouble(
        const Context &context, const Result &a, const Result &b) {
        return makeConvertExplicit(context, a, context, b);
    }

    void makeInitialize(const Context &context, llvm::Value *value, const utils::Typename &type) {
        if (!context.ir)
            return;

        bool status = handlers::resolve(
            std::array {
                handlers::makeInitializeNumber,
                handlers::makeInitializeReference,
                handlers::makeInitializeVariableArray,
                handlers::makeInitializeStruct,
                handlers::makeInitializeIgnore,
            },
            context, value, type);

        assert(status);
    }

    void makeDestroy(const Context &context, llvm::Value *value, const utils::Typename &type) {
        if (!context.ir)
            return;

        bool status = handlers::resolve(
            std::array {
                handlers::makeDestroyReference,
                handlers::makeDestroyUnique,
                handlers::makeDestroyVariableArray,
                handlers::makeDestroyRegular,
            },
            context, value, type);

        assert(status);

        // TODO: needs call to implicit object destructor, probably in BuilderType,
        // can do later
        // ^^ outside of try catch, so call doesn't fail and cancel destructing an
        // object we can also make builder function null if we dont have anything to
        // destroy, type X { a int, b int, c int } but idk if that's worth
    }
}