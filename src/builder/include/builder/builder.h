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

namespace llvm {
    struct Target;
    struct DataLayout;
    struct TargetMachine;
}

using namespace llvm;

struct IfNode;
struct ForNode;
struct CodeNode;
struct TypeNode;
struct BlockNode;
struct DebugNode;
struct AssignNode;
struct FunctionNode;
struct VariableNode;
struct ReferenceNode;
struct StatementNode;
struct ExpressionNode;

struct Builder;
struct BuilderScope;
struct BuilderFunction;

struct BuilderResult {
    enum class Kind {
        Raw,
        Reference,
        Literal
    };

    Kind kind = Kind::Raw;
    Value *value = nullptr;
    Typename type;

    std::shared_ptr<BuilderResult> implicit;

    BuilderResult(Kind kind, Value *value, Typename type, std::unique_ptr<BuilderResult> implicit = nullptr);
};

struct BuilderVariable {
    BuilderFunction &function;

    const VariableNode *node = nullptr;
    Typename type;

    Value *value = nullptr;

    BuilderVariable(const VariableNode *node, BuilderScope &scope); // regular variable
    BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope); // function parameter
};

struct BuilderScope {
    BuilderFunction &function;
    BuilderScope *parent = nullptr;

    BasicBlock *openingBlock = nullptr;
    BasicBlock *currentBlock = nullptr;

    std::optional<IRBuilder<>> current;

    // For ExpressionNode scopes, product is stored here
    std::optional<BuilderResult> product;

    // separate for now... for data efficiency - use findVariable function
    std::unordered_map<const VariableNode *, std::shared_ptr<BuilderVariable>> variables;

    BuilderVariable *findVariable(const VariableNode *node) const;

    // Node for search scope.
    std::optional<BuilderResult> convert(
        const BuilderResult &result, const Typename &type);
    static std::optional<std::pair<BuilderResult, BuilderResult>> convert(
        const BuilderResult &a, BuilderScope &aScope,
        const BuilderResult &b, BuilderScope &bScope);

    std::optional<std::pair<BuilderResult, BuilderResult>> convert(
        const BuilderResult &a, const BuilderResult &b);

    Value *get(const BuilderResult &result);
    Value *ref(const BuilderResult &result);

    BuilderResult makeExpressionNounContent(const Node *node);
    BuilderResult makeExpressionNounModifier(const Node *node, const BuilderResult &result);
    BuilderResult makeExpressionNoun(const ExpressionNoun &noun);
    BuilderResult makeExpressionOperation(const ExpressionOperation &operation);
    BuilderResult makeExpressionCombinator(const ExpressionCombinator &combinator);
    BuilderResult makeExpressionResult(const ExpressionResult &result);
    BuilderResult makeExpressionInferred(const BuilderResult &result);
    BuilderResult makeExpression(const ExpressionNode *node);

    void makeIf(const IfNode *node);
    void makeFor(const ForNode *node);
    void makeBlock(const BlockNode *node);
    void makeDebug(const DebugNode *node);
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

    Typename returnTypename;

    Type *returnType = nullptr;
    Value *returnValue = nullptr;

    FunctionTypename type;
    Function *function = nullptr;

    void build();

    BuilderFunction(const FunctionNode *node, Builder &builder);
};

struct BuilderTarget {
    std::string triple;
    const Target *target;
    TargetMachine *machine;
    std::unique_ptr<DataLayout> layout;

    [[nodiscard]] bool valid() const;

    explicit BuilderTarget(const std::string &suggestedTriple);
};

struct Builder {
    const RootNode *root = nullptr;

    Options options;

    std::unique_ptr<LLVMContext> context;
    std::unique_ptr<Module> module;

    BuilderTarget target;

    std::unordered_map<const TypeNode *, std::unique_ptr<BuilderType>> types;
    std::unordered_map<const FunctionNode *, std::unique_ptr<BuilderFunction>> functions;

    BuilderType *makeType(const TypeNode *node);
    BuilderFunction *makeFunction(const FunctionNode *node);

    static const Node *find(const ReferenceNode *node);
    static const TypeNode *find(const StackTypename &type);

    [[nodiscard]] Type *makeBuiltinTypename(const StackTypename &type) const;

    // Node needs to be passed to get a sense of scope.
    Type *makeStackTypename(const StackTypename &type);
    Type *makeTypename(const Typename &type);

    Builder(RootNode *root, Options opts);
};
