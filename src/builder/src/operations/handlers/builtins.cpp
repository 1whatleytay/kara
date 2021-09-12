#include <builder/builtins.h>

#include <utils/typename.h>

namespace kara::builder::ops::handlers::builtins {
//    using VerifyInput = ops::matching::MatchInputFlattened;
//    using VerifyPair = std::pair<std::string, utils::Typename>;

    // I wish, but I don't think it's going to happen
//    std::tuple<> verify(const VerifyInput &input, size_t index) {
//        throw; // makeConvert can only deal with concrete type, not any array of type ???
//    }

    bool named(const std::string &input, const std::string &required) {
        return input.empty() || input == required;
    }

    namespace arrays {
        Maybe<builder::Result> size(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!array)
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
                throw;

            case utils::ArrayKind::VariableSize: {
                auto real = makeReal(context, value);
                assert(std::holds_alternative<utils::ArrayTypename>(real.type));

                // mutable should probably be turned off after
                return builder::Result {
                    builder::Result::FlagReference | (real.flags & builder::Result::FlagMutable),
                    context.ir ? context.ir->CreateStructGEP(ops::ref(context, real), 0) : nullptr,
                    utils::PrimitiveTypename { utils::PrimitiveType::ULong },
                    context.accumulator,
                };
            }

            default:
                throw;
            }
        }

        Maybe<builder::Result> capacity(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto real = makeReal(context, value);
            assert(std::holds_alternative<utils::ArrayTypename>(real.type));

            return builder::Result {
                builder::Result::FlagReference | (real.flags & builder::Result::FlagMutable),
                context.ir ? context.ir->CreateStructGEP(ops::ref(context, real), 1) : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::ULong },
                context.accumulator,
            };
        }

        Maybe<builder::Result> data(const Context &context, const Parameters &parameters) {
            auto input = ops::matching::flatten(parameters);

            // name/size check
            if (!(input.size() == 1 && named(input[0].first, "array")))
                return std::nullopt;

            auto &value = input[0].second;

            auto subtype = ops::findReal(value.type);

            auto array = std::get_if<utils::ArrayTypename>(subtype);
            if (!(array && array->kind == utils::ArrayKind::VariableSize))
                return std::nullopt;

            auto real = makeReal(context, value);
            assert(std::holds_alternative<utils::ArrayTypename>(real.type));

            return builder::Result {
                builder::Result::FlagReference | (real.flags & builder::Result::FlagMutable),
                context.ir ? context.ir->CreateStructGEP(ops::ref(context, real), 2) : nullptr,
                utils::ReferenceTypename {
                    array->value,
                    real.isSet(builder::Result::FlagMutable),
                    utils::ReferenceKind::Regular,
                },
                context.accumulator,
            };
        }

        Maybe<builder::Result> resize(const Context &context, const Parameters &parameters) {
//            auto input = ops::matching::flatten(parameters);
//
//            // name/size check
//            if (!(input.size() == 2
//                    && named(input[0].first, "array")
//                    && named(input[1].first, "size")))
//                return std::nullopt;
//
//            auto &value = input[0].second;
//            auto &second = input[1].second;
//
//            auto subtype = ops::findReal(value.type);
//
//            auto array = std::get_if<utils::ArrayTypename>(subtype);
//            if (!(array && array->kind == utils::ArrayKind::VariableSize))
//                return std::nullopt;
//
//            auto ulongTypename = utils::PrimitiveTypename { utils::PrimitiveType::ULong };
//
//            auto converted = ops::makeConvert(context, second, ulongTypename);
//            if (!converted)
//                return std::nullopt;
//
//            auto &size = *converted;
//
//
            throw;
        }

        Maybe<builder::Result> reserve(const Context &context, const Parameters &parameters) {
            throw;
        }
    }

    std::vector<BuiltinFunction> matching(const std::string &name) {
        std::vector<BuiltinFunction> result;

        for (const auto &pair : functions) {
            if (pair.first == name) {
                result.push_back(pair.second);
            }
        }

        return result;
    }
}