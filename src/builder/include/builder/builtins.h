#pragma once

#include <builder/handlers.h>

// In the future, rework to actual name/type system and use regular match()
namespace kara::builder::ops::handlers::builtins {
    using Parameters = ops::matching::MatchInput;
    using BuiltinFunction = Maybe<builder::Result> (*)(const Context &, const Parameters &);

    // https://stackoverflow.com/a/25069819/16744931
    namespace utility {
        template <size_t... Is>
        struct seq { };
        template <size_t N, size_t... Is>
        struct gen_seq : gen_seq<N - 1, N - 1, Is...> { };
        template <size_t... Is>
        struct gen_seq<0, Is...> : seq<Is...> { };

        template <typename T, size_t N1, size_t... I1, size_t N2, size_t... I2>
        constexpr std::array<T, N1 + N2> concatImpl(
            const std::array<T, N1> &a1, const std::array<T, N2> &a2, seq<I1...>, seq<I2...>) {
            return { a1[I1]..., a2[I2]... };
        }

        template <typename T, size_t N1, size_t N2>
        constexpr std::array<T, N1 + N2> concatImpl(const std::array<T, N1> &a1, const std::array<T, N2> &a2) {
            return concatImpl(a1, a2, gen_seq<N1> {}, gen_seq<N2> {});
        }

        template <typename T, size_t N>
        constexpr std::array<T, N> concat(const std::array<T, N> &base) {
            return base;
        }

        template <typename T, size_t N1, size_t N2, typename... Args>
        constexpr auto concat(const std::array<T, N1> &base, const std::array<T, N2> &other, Args &&...args) {
            return concat(concatImpl(base, other), std::forward<Args>(args)...);
        }
    }

    namespace arrays {
        Maybe<builder::Result> size(const Context &context, const Parameters &parameters);
        Maybe<builder::Result> capacity(const Context &context, const Parameters &parameters);
        Maybe<builder::Result> data(const Context &context, const Parameters &parameters);

        Maybe<builder::Result> resize(const Context &context, const Parameters &parameters);
        Maybe<builder::Result> reserve(const Context &context, const Parameters &parameters);

        Maybe<builder::Result> add(const Context &context, const Parameters &parameters);
        Maybe<builder::Result> clear(const Context &context, const Parameters &parameters);

        Maybe<builder::Result> list(const Context &context, const Parameters &parameters);

        constexpr std::array functions = {
            std::make_pair("size", size),
            std::make_pair("capacity", capacity),
            std::make_pair("data", data),
            std::make_pair("resize", resize),
            std::make_pair("reserve", reserve),
            std::make_pair("add", add),
            std::make_pair("clear", clear),
            std::make_pair("list", list),
        };
    }

    // sorry, no flattening...
    constexpr auto functions = utility::concat(arrays::functions);

    std::vector<BuiltinFunction> matching(const std::string &name);
}