#pragma once
#include <options/options.h>

#include <parser/root.h>
#include <parser/typename.h>
#include <parser/expression.h>

#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

#include <queue>
#include <optional>
#include <unordered_set>
#include <unordered_map>

using namespace llvm;

struct IfNode;
struct ForNode;
struct NewNode;
struct CodeNode;
struct TypeNode;
struct BlockNode;
struct AssignNode;
struct ImportNode;
struct FunctionNode;
struct VariableNode;
struct ReferenceNode;
struct StatementNode;
struct ExpressionNode;

struct Manager;
struct ManagerFile;

struct Builder;
struct BuilderScope;
struct BuilderResult;
struct BuilderFunction;
struct BuilderStatementContext;

struct BuilderResult {
    enum class Kind {
        Raw,
        Reference,
        Literal,

        Unresolved
    };

    Kind kind = Kind::Raw;
    Value *value = nullptr;
    Typename type;

    std::shared_ptr<BuilderResult> implicit;

    const Node *from = nullptr;
    std::vector<const Node *> references;

    uint64_t statementUID = 0; // copied when needed, for reference in move without refactor

    const Node *first(::Kind nodeKind);

    BuilderResult(Kind kind, Value *value, Typename type, BuilderStatementContext *statementContext,
        std::unique_ptr<BuilderResult> implicit = nullptr);
    BuilderResult(const Node *from, std::vector<const Node *> references, BuilderStatementContext *statementContext,
        std::unique_ptr<BuilderResult> implicit = nullptr);
};


// Thinking struct for destroying objects when a statement is done.
struct BuilderStatementContext {
    // COUPLING AHH T_T I'm sorry...
    // I need it to use invokeDestroy for now, would be best to separate everything to global scope but...
    BuilderScope &parent;

    uint64_t nextUID = 1;
    uint64_t getNextUID();

    bool lock = false; // for debugging consider/commit loops

    std::queue<BuilderResult> toDestroy;
    std::unordered_set<uint64_t> avoidDestroy;

    void consider(const BuilderResult &result);

    void commit(BasicBlock *block);

    explicit BuilderStatementContext(BuilderScope &parent);
};

struct BuilderVariable {
    const VariableNode *node = nullptr;
    Typename type;

    Value *value = nullptr;

    BuilderVariable(const VariableNode *node, Builder &builder); // global variable
    BuilderVariable(const VariableNode *node, BuilderScope &scope); // regular variable
    BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope); // function parameter
};

struct MatchResult {
    std::optional<std::string> failed;
    std::vector<const BuilderResult *> map;

    size_t numImplicit = 0;
};

struct MatchInput {
    std::vector<const BuilderResult *> parameters;
    std::unordered_map<size_t, std::string> names;
};

struct MatchCallError {
    std::string problem;
    std::vector<std::string> messages;
};

struct BuilderScope {
    Builder &builder;
    BuilderScope *parent = nullptr;
    BuilderFunction *function = nullptr;

    BuilderStatementContext statementContext;

    BasicBlock *openingBlock = nullptr;
    BasicBlock *currentBlock = nullptr;

    BasicBlock *lastBlock = nullptr;

    Value *exitChainType = nullptr;
    BasicBlock *exitChainBegin = nullptr;

    enum class ExitPoint {
        Regular,
        Return,
        Break,
        Continue
    };

    std::set<ExitPoint> requiredPoints = { ExitPoint::Regular };
    std::unordered_map<ExitPoint, BasicBlock *> destinations;

    void commit();
    void exit(ExitPoint point, BasicBlock *from = nullptr);

    std::optional<IRBuilder<>> current;

    // For ExpressionNode scopes, product is stored here
    std::optional<BuilderResult> product;

    // separate for now... for data efficiency - use findVariable function
    std::unordered_map<const VariableNode *, std::shared_ptr<BuilderVariable>> variables;

    MatchResult match(
        const std::vector<const VariableNode *> &variables, const MatchInput &input);
    std::variant<BuilderResult, MatchCallError> call(
        const std::vector<const Node *> &options, const MatchInput &input);
    std::variant<BuilderResult, MatchCallError> call(
        const std::vector<const Node *> &options, const MatchInput &input, IRBuilder<> *builder);

    static BuilderResult callUnpack(const std::variant<BuilderResult, MatchCallError> &result, const Node *node);

    BuilderVariable *findVariable(const VariableNode *node) const;

    // Node for search scope.
    static std::optional<Typename> negotiate(const Typename &left, const Typename &right);

    std::optional<BuilderResult> convert(
        const BuilderResult &result, const Typename &type, bool force = false);
    static std::optional<std::pair<BuilderResult, BuilderResult>> convert(
        const BuilderResult &a, BuilderScope &aScope,
        const BuilderResult &b, BuilderScope &bScope);
    std::optional<std::pair<BuilderResult, BuilderResult>> convert(
        const BuilderResult &a, const BuilderResult &b);

    BuilderResult infer(const BuilderResult &result);
    BuilderResult unpack(const BuilderResult &result);
    BuilderResult pass(const BuilderResult &result);

    void invokeDestroy(const BuilderResult &result);
    void invokeDestroy(const BuilderResult &result, IRBuilder<> &builder);
    void invokeDestroy(const BuilderResult &result, BasicBlock *block);

    Value *get(const BuilderResult &result);
    Value *ref(const BuilderResult &result);
    Value *get(const BuilderResult &result, IRBuilder<> &builder) const;
    Value *ref(const BuilderResult &result, IRBuilder<> &builder) const;

    BuilderResult combine(const BuilderResult &a, const BuilderResult &b, OperatorNode::Operation op);

    BuilderResult makeExpressionNounContent(const Node *node);
    BuilderResult makeExpressionNounModifier(const Node *node, const BuilderResult &result);
    BuilderResult makeExpressionNoun(const ExpressionNoun &noun);
    BuilderResult makeExpressionOperation(const ExpressionOperation &operation);
    BuilderResult makeExpressionCombinator(const ExpressionCombinator &combinator);
    BuilderResult makeExpressionResult(const ExpressionResult &result);
    BuilderResult makeExpression(const ExpressionNode *node);

    BuilderResult makeNew(const NewNode *node);

    void makeIf(const IfNode *node);
    void makeFor(const ForNode *node);
    void makeBlock(const BlockNode *node);
    void makeAssign(const AssignNode *node);
    void makeStatement(const StatementNode *node);

    BuilderScope(const Node *node, BuilderScope &parent, bool doCodeGen = true);
    BuilderScope(const Node *node, BuilderFunction &function, bool doCodeGen = true);

private:
    void makeParameters();

    BuilderScope(const Node *node, BuilderFunction &function, BuilderScope *parent, bool doCodeGen = true);
};

struct BuilderType {
    Builder &builder;
    const TypeNode *node = nullptr;

    StructType *type = nullptr;

    std::unordered_map<const VariableNode *, size_t> indices;

    BuilderFunction *implicitDestructor = nullptr;

    // for avoiding recursive problems
    void build();

    explicit BuilderType(const TypeNode *node, Builder &builder);
};

struct BuilderFunction {
    enum class Purpose {
        UserFunction,
        TypeDestructor,
    };

    Purpose purpose = Purpose::UserFunction;

    Builder &builder;

    const Node *node = nullptr;

    BasicBlock *entryBlock = nullptr;
    BasicBlock *exitBlock = nullptr;

    IRBuilder<> entry;
    IRBuilder<> exit;

    Type *returnType = nullptr;
    Value *returnValue = nullptr;

    FunctionTypename type;
    Function *function = nullptr;

    void build();

    BuilderFunction(const Node *node, Builder &builder);
};

struct Builder {
    const RootNode *root = nullptr;

    const ManagerFile &file;
    const Options &options;

    Function *mallocCache = nullptr;
    Function *freeCache = nullptr;

    std::set<const ManagerFile *> dependencies;

    LLVMContext &context;
    std::unique_ptr<Module> module;

    std::vector<const Node *> destroyInvokables;

    std::unordered_map<const TypeNode *, std::unique_ptr<BuilderFunction>> implicitDestructors;

    std::unordered_map<const TypeNode *, std::unique_ptr<BuilderType>> types;
    std::unordered_map<const VariableNode *, std::unique_ptr<BuilderVariable>> globals;
    std::unordered_map<const FunctionNode *, std::unique_ptr<BuilderFunction>> functions;

    BuilderType *makeType(const TypeNode *node);
    BuilderVariable *makeGlobal(const VariableNode *node);
    BuilderFunction *makeFunction(const FunctionNode *node);

    Function *getMalloc();
    Function *getFree();

    const Node *find(const ReferenceNode *node);
    std::vector<const Node *> findAll(const ReferenceNode *node);

    const Node *searchDependencies(const std::function<bool(Node *)> &match);
    std::vector<const Node *> searchAllDependencies(const std::function<bool(Node *)> &match);

    Typename resolveTypename(const Node *node);

    Type *makeTypename(const Typename &type);
    [[nodiscard]] Type *makePrimitiveType(PrimitiveType type) const;

    Builder(const ManagerFile &file, const Options &opts);
};
