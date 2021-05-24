#pragma once

#include <options/options.h>

#include <parser/root.h>
#include <parser/typename.h>
#include <parser/expression.h>

#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

#include <optional>
#include <unordered_map>

using namespace llvm;

struct IfNode;
struct ForNode;
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
struct BuilderFunction;

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

    const Node *first(::Kind nodeKind);

    BuilderResult(Kind kind, Value *value, Typename type,
        std::unique_ptr<BuilderResult> implicit = nullptr);
    BuilderResult(const Node *from, std::vector<const Node *> references,
        std::unique_ptr<BuilderResult> implicit = nullptr);
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
    std::optional<int64_t> failed;

    size_t numImplicit = 0;
};

struct BuilderScope {
    BuilderFunction &function;
    BuilderScope *parent = nullptr;

    BasicBlock *openingBlock = nullptr;
    BasicBlock *currentBlock = nullptr;

    BasicBlock *breakBlock = nullptr;
    BasicBlock *continueBlock = nullptr;

    std::optional<IRBuilder<>> current;

    // For ExpressionNode scopes, product is stored here
    std::optional<BuilderResult> product;

    // separate for now... for data efficiency - use findVariable function
    std::unordered_map<const VariableNode *, std::shared_ptr<BuilderVariable>> variables;

    MatchResult match(
        const FunctionNode *node, const std::vector<BuilderResult *> &parameters);
    BuilderResult call(
        const std::vector<const FunctionNode *> &options, const std::vector<BuilderResult *> &parameters);

    BuilderVariable *findVariable(const VariableNode *node) const;

    // Node for search scope.
    std::optional<BuilderResult> convert(
        const BuilderResult &result, const Typename &type, bool force = false);
    static std::optional<std::pair<BuilderResult, BuilderResult>> convert(
        const BuilderResult &a, BuilderScope &aScope,
        const BuilderResult &b, BuilderScope &bScope);

    BuilderResult convertOrThrow(
        const Node *node, const BuilderResult &result, const Typename &type);

    BuilderResult infer(const BuilderResult &result);
    BuilderResult unpack(const BuilderResult &result);

    std::optional<std::pair<BuilderResult, BuilderResult>> convert(
        const BuilderResult &a, const BuilderResult &b);

    Value *get(const BuilderResult &result);
    Value *ref(const BuilderResult &result);

    BuilderResult combine(const BuilderResult &a, const BuilderResult &b, OperatorNode::Operation op);

    BuilderResult makeExpressionNounContent(const Node *node);
    BuilderResult makeExpressionNounModifier(const Node *node, const BuilderResult &result);
    BuilderResult makeExpressionNoun(const ExpressionNoun &noun);
    BuilderResult makeExpressionOperation(const ExpressionOperation &operation);
    BuilderResult makeExpressionCombinator(const ExpressionCombinator &combinator);
    BuilderResult makeExpressionResult(const ExpressionResult &result);
    BuilderResult makeExpression(const ExpressionNode *node);

    void makeIf(const IfNode *node);
    void makeFor(const ForNode *node);
    void makeBlock(const BlockNode *node);
    void makeAssign(const AssignNode *node);
    void makeStatement(const StatementNode *node);

    BuilderScope(const Node *node, BuilderScope &parent, bool doCodeGen = true,
        BasicBlock *breakBlock = nullptr, BasicBlock *continueBlock = nullptr);
    BuilderScope(const Node *node, BuilderFunction &function, bool doCodeGen = true);

private:
    void makeParameters();

    BuilderScope(const Node *node, BuilderFunction &function, BuilderScope *parent, bool doCodeGen = true,
        BasicBlock *breakBlock = nullptr, BasicBlock *continueBlock = nullptr);
};

struct BuilderType {
    Builder &builder;
    const TypeNode *node = nullptr;

    StructType *type = nullptr;

    std::unordered_map<const VariableNode *, size_t> indices;

    // for avoiding recursive problems
    void build();

    explicit BuilderType(const TypeNode *node, Builder &builder);
};

struct BuilderFunction {
    Builder &builder;

    const FunctionNode *node = nullptr;

    BasicBlock *entryBlock = nullptr;
    BasicBlock *exitBlock = nullptr;

    IRBuilder<> entry;
    IRBuilder<> exit;

    Type *returnType = nullptr;
    Value *returnValue = nullptr;

    FunctionTypename type;
    Function *function = nullptr;

    void build();

    BuilderFunction(const FunctionNode *node, Builder &builder);
};

struct Builder {
    const RootNode *root = nullptr;

    const ManagerFile &file;
    const Options &options;

    std::set<const ManagerFile *> dependencies;

    LLVMContext &context;
    std::unique_ptr<Module> module;

    std::unordered_map<const TypeNode *, std::unique_ptr<BuilderType>> types;
    std::unordered_map<const VariableNode *, std::unique_ptr<BuilderVariable>> globals;
    std::unordered_map<const FunctionNode *, std::unique_ptr<BuilderFunction>> functions;

    BuilderType *makeType(const TypeNode *node);
    BuilderVariable *makeGlobal(const VariableNode *node);
    BuilderFunction *makeFunction(const FunctionNode *node);

    const Node *find(const ReferenceNode *node);
    std::vector<const Node *> findAll(const ReferenceNode *node);

    const Node *searchDependencies(const std::function<bool(Node *)> &match);
    std::vector<const Node *> searchAllDependencies(const std::function<bool(Node *)> &match);

    Typename resolveTypename(const Node *node);

    Type *makeTypename(const Typename &type);
    [[nodiscard]] Type *makePrimitiveType(PrimitiveType type) const;

    Builder(const ManagerFile &file, const Options &opts);
};
