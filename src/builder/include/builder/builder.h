#pragma once

#include <options/options.h>

#include <utils/typename.h>
#include <utils/expression.h>

#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

#include <set>
#include <queue>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace kara::parser {
    struct If;
    struct For;
    struct New;
    struct Code;
    struct Root;
    struct Type;
    struct Block;
    struct Assign;
    struct Import;
    struct Function;
    struct Variable;
    struct Reference;
    struct Statement;
    struct Expression;

    enum class Kind;
}

namespace kara::builder {
    struct Manager;
    struct ManagerFile;

    struct Builder;

    struct Scope;
    struct Result;
    struct Function;
    struct StatementContext;

    struct Result {
        // valid flag layouts: reference, temporary | mutable | variable
        enum Flags : uint32_t {
            FlagTemporary = 1u << 0u,
            FlagMutable = 1u << 1u,
            FlagReference = 1u << 2u
        };

        uint32_t flags = 0;
        [[nodiscard]] bool isSet(Flags flag) const;

        uint64_t statementUID = 0; // copied when needed, for reference in move without refactor

        llvm::Value *value = nullptr;
        utils::Typename type;

        Result(
            uint32_t flags,
            llvm::Value *value,
            utils::Typename type,
            StatementContext *statementContext);
    };

    struct Unresolved {
        const hermes::Node *from = nullptr;
        std::vector<const hermes::Node *> references;

        std::shared_ptr<Result> implicit;

        Unresolved(
            const hermes::Node *from,
            std::vector<const hermes::Node *> references,
            std::unique_ptr<Result> implicit = nullptr);
    };

    using Wrapped = std::variant<Result, Unresolved>;

    // Thinking struct for destroying objects when a statement is done.
    struct StatementContext {
        // COUPLING AHH T_T I'm sorry...
        // I need it to use invokeDestroy for now, would be best to separate everything to global scope but...
        // DW i got u - future taylor
        Scope &parent;

        uint64_t nextUID = 1;
        uint64_t getNextUID();

        bool lock = false; // for debugging consider/commit loops

        std::queue<Result> toDestroy;
        std::unordered_set<uint64_t> avoidDestroy;

        void consider(const Result &result);

        void commit(llvm::BasicBlock *block);

        explicit StatementContext(Scope &parent);
    };

    struct Variable {
        const parser::Variable *node = nullptr;
        utils::Typename type;

        llvm::Value *value = nullptr;

        Variable(const parser::Variable *node, Builder &builder); // global variable
        Variable(const parser::Variable *node, Scope &scope); // regular variable
        Variable(const parser::Variable *node, llvm::Value *input, Scope &scope); // function parameter
    };

    struct MatchResult {
        std::optional<std::string> failed;
        std::vector<const Result *> map;

        size_t numImplicit = 0;
    };

    struct MatchInput {
        std::vector<const Result *> parameters;
        std::unordered_map<size_t, std::string> names;
    };

    struct MatchCallError {
        std::string problem;
        std::vector<std::string> messages;
    };

    struct Scope {
        Builder &builder;
        Scope *parent = nullptr;
        Function *function = nullptr;

        StatementContext statementContext;

        llvm::BasicBlock *openingBlock = nullptr;
        llvm::BasicBlock *currentBlock = nullptr;

        llvm::BasicBlock *lastBlock = nullptr;

        llvm::Value *exitChainType = nullptr;
        llvm::BasicBlock *exitChainBegin = nullptr;

        enum class ExitPoint {
            Regular,
            Return,
            Break,
            Continue
        };

        std::set<ExitPoint> requiredPoints = { ExitPoint::Regular };
        std::unordered_map<ExitPoint, llvm::BasicBlock *> destinations;

        // For types, might need to be cleared occasionally. For ArrayKind::UnboundedSize mostly.
        std::unordered_map<const parser::Expression *, Result> expressionCache;

        llvm::Value *makeAlloca(const utils::Typename &type, const std::string &name = "");
        llvm::Value *makeMalloc(const utils::Typename &type, const std::string &name = "");

        void commit();
        void exit(ExitPoint point, llvm::BasicBlock *from = nullptr);

        std::optional<llvm::IRBuilder<>> current;

        // For ExpressionNode scopes, product is stored here
        std::optional<Result> product;

        // separate for now... for data efficiency - use findVariable function
        std::unordered_map<const parser::Variable *, std::shared_ptr<Variable>> variables;

        MatchResult match(
            const std::vector<const parser::Variable *> &variables, const MatchInput &input);
        std::variant<Result, MatchCallError> call(
            const std::vector<const hermes::Node *> &options, const MatchInput &input);
        std::variant<Result, MatchCallError> call(
            const std::vector<const hermes::Node *> &options, const MatchInput &input, llvm::IRBuilder<> *builder);

        static Result callUnpack(const std::variant<Result, MatchCallError> &result, const hermes::Node *node);

        Variable *findVariable(const parser::Variable *node) const;

        // Node for search scope.
        static std::optional<utils::Typename> negotiate(const utils::Typename &left, const utils::Typename &right);

        std::optional<Result> convert(
            const Result &result, const utils::Typename &type, bool force = false);
        static std::optional<std::pair<Result, Result>> convert(
            const Result &a, Scope &aScope,
            const Result &b, Scope &bScope);
        std::optional<std::pair<Result, Result>> convert(
            const Result &a, const Result &b);

        Result infer(const Wrapped &result);
        Result unpack(const Result &result);
        Result pass(const Result &result);

        void invokeDestroy(const builder::Result &result);
        void invokeDestroy(const builder::Result &result, llvm::IRBuilder<> &builder);
        void invokeDestroy(const builder::Result &result, llvm::BasicBlock *block);

        llvm::Value *get(const builder::Result &result);
        llvm::Value *ref(const builder::Result &result);
        llvm::Value *get(const builder::Result &result, llvm::IRBuilder<> &builder) const;
        llvm::Value *ref(const builder::Result &result, llvm::IRBuilder<> &builder) const;

        Result combine(const Result &a, const Result &b, utils::BinaryOperation op);

        Wrapped makeExpressionNounContent(const hermes::Node *node);
        Wrapped makeExpressionNounModifier(const hermes::Node *node, const Wrapped &result);
        Wrapped makeExpressionNoun(const utils::ExpressionNoun &noun);
        Wrapped makeExpressionOperation(const utils::ExpressionOperation &operation);
        Wrapped makeExpressionCombinator(const utils::ExpressionCombinator &combinator);
        Wrapped makeExpressionResult(const utils::ExpressionResult &result);
        Result makeExpression(const parser::Expression *node);

        Result makeNew(const parser::New *node);

        void makeIf(const parser::If *node);
        void makeFor(const parser::For *node);
        void makeBlock(const parser::Block *node);
        void makeAssign(const parser::Assign *node);
        void makeStatement(const parser::Statement *node);

        Scope(const hermes::Node *node, Scope &parent, bool doCodeGen = true);
        Scope(const hermes::Node *node, Function &function, bool doCodeGen = true);

    private:
        void makeParameters();

        Scope(const hermes::Node *node, Function &function, Scope *parent, bool doCodeGen = true);
    };

    struct Type {
        Builder &builder;
        const parser::Type *node = nullptr;

        llvm::StructType *type = nullptr;

        std::unordered_map<const parser::Variable *, size_t> indices;

        Function *implicitDestructor = nullptr;

        // for avoiding recursive problems
        void build();

        explicit Type(const parser::Type *node, Builder &builder);
    };

    struct Function {
        enum class Purpose {
            UserFunction,
            TypeDestructor,
        };

        Purpose purpose = Purpose::UserFunction;

        Builder &builder;

        const hermes::Node *node = nullptr;

        llvm::BasicBlock *entryBlock = nullptr;
        llvm::BasicBlock *exitBlock = nullptr;

        llvm::IRBuilder<> entry;
        llvm::IRBuilder<> exit;

        llvm::Type *returnType = nullptr;
        llvm::Value *returnValue = nullptr;

        utils::FunctionTypename type;
        llvm::Function *function = nullptr;

        void build();

        Function(const hermes::Node *node, Builder &builder);
    };

    struct Builder {
        const parser::Root *root = nullptr;

        const ManagerFile &file;
        const options::Options &options;

        llvm::Function *mallocCache = nullptr;
        llvm::Function *freeCache = nullptr;

        std::unordered_set<const ManagerFile *> dependencies;

        llvm::LLVMContext &context;
        std::unique_ptr<llvm::Module> module;

        std::vector<const hermes::Node *> destroyInvokables;

        std::unordered_map<const parser::Type *, std::unique_ptr<Function>> implicitDestructors;

        std::unordered_map<const parser::Type *, std::unique_ptr<builder::Type>> types;
        std::unordered_map<const parser::Variable *, std::unique_ptr<builder::Variable>> globals;
        std::unordered_map<const parser::Function *, std::unique_ptr<builder::Function>> functions;

        builder::Type *makeType(const parser::Type *node);
        builder::Variable *makeGlobal(const parser::Variable *node);
        builder::Function *makeFunction(const parser::Function *node);

        llvm::Function *getMalloc();
        llvm::Function *getFree();

        const hermes::Node *find(const parser::Reference *node);
        std::vector<const hermes::Node *> findAll(const parser::Reference *node);

        using SearchChecker = std::function<bool(const hermes::Node *)>;

        const hermes::Node *searchDependencies(const SearchChecker &match);
        std::vector<const hermes::Node *> searchAllDependencies(const SearchChecker &match);

        utils::Typename resolveTypename(const hermes::Node *node);

        llvm::Type *makeTypename(const utils::Typename &type);
        [[nodiscard]] llvm::Type *makePrimitiveType(utils::PrimitiveType type) const;

        Builder(const ManagerFile &file, const options::Options &opts);
    };
}
