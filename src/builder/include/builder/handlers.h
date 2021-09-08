#pragma once

#include <builder/builder.h>
#include <builder/operations.h>

#include <utils/typename.h>

#include <optional>

namespace kara::builder::ops::handlers {
    template <typename T>
    using Maybe = std::optional<T>;

    template <size_t size, typename... Args, typename... Others>
    bool resolve(const std::array<bool (*)(Args...), size> &values, Others &&...args) {
        static_assert(std::is_invocable_v<bool (*)(Args...), Others...>);

        return std::any_of(values.begin(), values.end(), [&args...](auto f) { return f(args...); });
    }

    template <size_t size, typename T, typename... Args, typename... Others>
    Maybe<T> resolve(const std::array<Maybe<T> (*)(Args...), size> &values, Others &&...args) {
        static_assert(std::is_invocable_v<Maybe<T> (*)(Args...), Others...>);

        for (const auto &f : values) {
            auto v = f(args...);

            if (v)
                return *v;
        }

        return std::nullopt;
    }

    template <typename F, typename T>
    T bridge(const F &f, T input) {
        static_assert(std::is_same_v<std::invoke_result_t<F, T>, Maybe<T>>);

        while (true) {
            Maybe<T> value = f(input);

            if (!value)
                return input;

            input = *value;
        }
    }

    Maybe<utils::Typename> negotiateEqual(const utils::Typename &left, const utils::Typename &right);
    Maybe<utils::Typename> negotiatePrimitive(const utils::Typename &left, const utils::Typename &right);

    Maybe<builder::Result> makeConvertBridgeImplicitReference(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force); // promote
    Maybe<builder::Result> makeConvertBridgeImplicitDereference(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force); // demote

    Maybe<builder::Result> makeConvertEqual(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertForcedRefToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertForcedULongToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertForcedRefToULong(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertUniqueOrMutableToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertRefToAnyRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertRefToUnboundedRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertFixedRefToUnboundedRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertNullToRef(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertRefToBool(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertIntToFloat(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertFloatToInt(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);
    Maybe<builder::Result> makeConvertPrimitiveExtend(
        const Context &context, const builder::Result &result, const utils::Typename &type, bool force);

    Maybe<builder::Result> makeNotWithBool(const Context &context, const builder::Result &value);
    Maybe<builder::Result> makeNegativeWithNumber(const Context &context, const builder::Result &value);
    Maybe<builder::Result> makeReferenceWithVariable(const Context &context, const builder::Result &value);
    Maybe<builder::Result> makeDereferenceWithReference(const Context &context, const builder::Result &value);

    Maybe<builder::Result> makeAddNumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeSubNumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeMulNumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeDivNumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeModInt(
        const Context &context, const builder::Result &left, const builder::Result &right);

    Maybe<builder::Result> makeEQNumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeEQRef(const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeNENumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeNERef(const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeGTNumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeGENumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeLTNumber(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeLENumber(
        const Context &context, const builder::Result &left, const builder::Result &right);

    Maybe<builder::Result> makeOrBool(
        const Context &context, const builder::Result &left, const builder::Result &right);
    Maybe<builder::Result> makeAndBool(
        const Context &context, const builder::Result &left, const builder::Result &right);

    // here it's to be being more specific, we need a fix to the multiple code generation problem though
    Maybe<builder::Wrapped> makeCallOnNew(
        const Context &context, const builder::Unresolved &unresolved, const matching::MatchInput &input);
    Maybe<builder::Wrapped> makeCallOnFunctionOrType(
        const Context &context, const builder::Unresolved &unresolved, const matching::MatchInput &input);

    // Marked as taking builder::Result to avoid multiple infers... new solution maybe be needed in future
//    Maybe<builder::Wrapped> makeDotForArrayProperties(
//        const Context &context, const builder::Result &value, const parser::Reference *node);
    Maybe<builder::Wrapped> makeDotForField(
        const Context &context, const builder::Result &value, const parser::Reference *node);
    Maybe<builder::Wrapped> makeDotForUFCS(
        const Context &context, const builder::Result &value, const parser::Reference *node);

    bool makeDestroyReference(const Context &context, const builder::Result &result); // block it
    bool makeDestroyUnique(const Context &context, const builder::Result &result);
    bool makeDestroyGlobal(const Context &context, const builder::Result &result);
}