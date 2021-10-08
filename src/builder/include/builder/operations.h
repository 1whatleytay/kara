#pragma once

#include <builder/builder.h>
#include <builder/error.h>

#include <utils/literals.h>

#include <optional>
#include <unordered_set>

namespace kara::builder::ops {
    template <typename T, typename... Args>
    std::invoke_result_t<T, Args...> blame(const hermes::Node *fault, T t, Args &&...args) {
        static_assert(std::is_invocable_v<T, Args...>);

        try {
            return t(std::forward<Args>(args)...);
        } catch (const std::runtime_error &e) { throw builder::VerifyError(fault, "{}", e.what()); }
    }

    template <typename... Args>
    void die(const char *format, Args &&...args) {
        throw std::runtime_error(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename T, typename... Args>
    T die(const std::optional<T> &result, const char *format, Args &&...args) {
        if (result)
            return *result;

        die(format, std::forward<Args>(args)...);
        throw;
    }

    struct ExitInfo {
        llvm::BasicBlock *exitChainEnd = nullptr;
        llvm::BasicBlock *exitChainBegin = nullptr;
        llvm::Value *exitChainType = nullptr;
    };

    struct Context {
        builder::Builder &builder;
        builder::Accumulator *accumulator = nullptr;

        llvm::IRBuilder<> *ir = nullptr;

        builder::Cache *cache = nullptr;
        builder::Function *function = nullptr; // replace with entry?

        ExitInfo *exitInfo = nullptr;

        [[nodiscard]] Context noIR() const;
        [[nodiscard]] Context move(llvm::IRBuilder<> *ir) const;

        //        static Context from(builder::Scope &scope);
    };

    llvm::Value *get(const Context &context, const builder::Result &result);
    llvm::Value *ref(const Context &context, const builder::Result &result);

    std::optional<utils::Typename> negotiate(const utils::Typename &left, const utils::Typename &right);

    // some missing:
    // make create -> zero init arrays ([int] size must be 0), default constructor for default field values
    // make copy -> override for [int], for unique pointers, etc.
    // make move -> move operator, for unique pointers, etc.
    // we have makeDestroy I think, we should rename

    builder::Result makePass(const Context &context, const Result &result);
    builder::Result makeInfer(const Context &context, const Wrapped &result);

    const utils::Typename *findReal(const utils::Typename &result);
    builder::Result makeReal(const Context &context, const Result &result);

    llvm::Value *makeAlloca(const Context &context, const utils::Typename &type, const std::string &name = "");
    llvm::Value *makeMalloc(const Context &context, const utils::Typename &type, const std::string &name = "");

    std::optional<builder::Result> makeConvert(
        const Context &context, const builder::Result &value, const utils::Typename &type, bool force = false);

    std::optional<std::pair<Result, Result>> makeConvertExplicit(
        const Context &aContext, const Result &a, const Context &bContext, const Result &b);
    std::optional<std::pair<Result, Result>> makeConvertDouble(
        const Context &context, const Result &a, const Result &b);

    void makeInitialize(const Context &context, llvm::Value *value, const utils::Typename &type);
    void makeDestroy(const Context &context, llvm::Value *value, const utils::Typename &type);

    namespace nouns {
        builder::Result makeSpecial(const Context &context, utils::SpecialType type);
        builder::Result makeBool(const Context &context, bool value);
        builder::Result makeNumber(const Context &context, const utils::NumberValue &value);
        builder::Result makeString(const Context &context, const std::string &text,
            const std::unordered_map<size_t, builder::Result> &inserts = {});
        builder::Result makeArray(const Context &context, const std::vector<builder::Result> &values);
        builder::Result makeNew(const Context &context, const utils::Typename &type);
    }

    namespace unary {
        builder::Result makeNot(const Context &context, const builder::Result &value);
        builder::Result makeNegative(const Context &context, const builder::Result &value);
        builder::Result makeReference(const Context &context, const builder::Result &value);
        builder::Result makeDereference(const Context &context, const builder::Result &value);
    }

    namespace binary {
        builder::Result makeAdd(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeSub(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeMul(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeDiv(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeMod(const Context &context, const builder::Result &left, const builder::Result &right);

        builder::Result makeEQ(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeNE(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeGT(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeGE(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeLT(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeLE(const Context &context, const builder::Result &left, const builder::Result &right);

        builder::Result makeOr(const Context &context, const builder::Result &left, const builder::Result &right);
        builder::Result makeAnd(const Context &context, const builder::Result &left, const builder::Result &right);

        builder::Result makeFallback(const Context &context, const builder::Result &left, const builder::Result &right);
    }

    namespace modifiers {
        builder::Wrapped makeCall(const Context &context, const builder::Wrapped &value, const parser::Call *node);
        builder::Wrapped makeDot(const Context &context, const builder::Wrapped &value, const parser::Dot *node);
        builder::Wrapped makeIndex(const Context &context, const builder::Wrapped &value, const parser::Index *node);
        builder::Wrapped makeTernary(
            const Context &context, const builder::Wrapped &value, const parser::Ternary *node);
        builder::Wrapped makeAs(const Context &context, const builder::Wrapped &value, const parser::As *node);
    }

    namespace expression {
        builder::Wrapped makeNounContent(const Context &context, const hermes::Node *node);
//        builder::Wrapped makeNounModifier(
//            const Context &context, const builder::Wrapped &value, const hermes::Node *node);

        builder::Wrapped makeUnary(const Context &context, const builder::Result &result, const parser::Unary *node);

        builder::Wrapped makeNoun(const Context &context, const utils::ExpressionNoun &noun);
        builder::Wrapped makeOperation(const Context &context, const utils::ExpressionOperation &operation);
        builder::Wrapped makeCombinator(const Context &context, const utils::ExpressionCombinator &combinator);
        builder::Wrapped makeResult(const Context &context, const utils::ExpressionResult &result);

        builder::Result make(const Context &context, const parser::Expression *expression);
    }

    namespace matching {
        struct MatchResult {
            std::optional<std::string> failed;
            std::vector<builder::Result> map;

            size_t numImplicit = 0;
        };

        struct MatchInput {
            std::vector<builder::Result> parameters;
            std::unordered_map<size_t, std::string> names;
        };

        // Probably best to slowly transition to this structure instead of MatchInput?
        using MatchInputFlattened = std::vector<std::pair<std::string, builder::Result>>;

        MatchInputFlattened flatten(const MatchInput &input);

        struct CallError {
            std::string problem;
            std::vector<std::string> messages;
        };

        using CallWrapped = std::variant<builder::Result, CallError>;

        MatchResult match(
            Builder &builder, const std::vector<const parser::Variable *> &variables, const MatchInput &input);

        CallWrapped call(const Context &context, const std::vector<const hermes::Node *> &options,
            const std::vector<ops::handlers::builtins::BuiltinFunction> &builtins, const MatchInput &input);

        builder::Result unwrap(const CallWrapped &result, const hermes::Node *node);
    }

    namespace statements {
        void exit(const Context &context, ExitPoint point);

        void makeIf(const Context &context, const parser::If *node);
        void makeFor(const Context &context, const parser::For *node);
        void makeBlock(const Context &context, const parser::Block *node);
        void makeAssign(const Context &context, const parser::Assign *node);
        void makeStatement(const Context &context, const parser::Statement *node);

        using Destinations = std::unordered_map<builder::ExitPoint, llvm::BasicBlock *>;

        llvm::BasicBlock *makeScope(const Context &context, const parser::Code *node, const Destinations &destinations);
    }
}
