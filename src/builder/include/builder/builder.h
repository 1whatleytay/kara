#pragma once

#include <builder/lifetime/lifetime.h>

#include <options/options.h>

#include <parser/root.h>
#include <parser/typename.h>
#include <parser/expression.h>

#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

#include <unordered_map>

using namespace llvm;

struct IfNode;
struct ForNode;
struct CodeNode;
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

    int32_t lifetimeDepth = 0;
    std::shared_ptr<MultipleLifetime> lifetime;

    BuilderResult(Kind kind, Value *value, Typename type,
        int32_t lifetimeDepth = 0, std::shared_ptr<MultipleLifetime> lifetime = std::make_shared<MultipleLifetime>());
};

struct BuilderVariable {
    BuilderFunction &function;

    const VariableNode *node = nullptr;
    Typename type;

    Value *value = nullptr;

    std::shared_ptr<MultipleLifetime> lifetime;

    BuilderVariable(const VariableNode *node, BuilderScope &scope); // regular variable
    BuilderVariable(const VariableNode *node, Value *input, BuilderScope &scope); // function parameter

private:
    [[nodiscard]] std::shared_ptr<MultipleLifetime> makeExpressionLifetime() const;
};

struct BuilderVariableInfo {
    BuilderVariable &variable;
    // regress to regular reference in future if you think its okay
    const std::shared_ptr<MultipleLifetime> &lifetime;
};

struct BuilderScope {
    BuilderFunction &function;
    BuilderScope *parent = nullptr;

    BasicBlock *openingBlock = nullptr;
    BasicBlock *currentBlock = nullptr;

    IRBuilder<> current;

    // separate for now... for data efficiency - use findVariable function
    std::unordered_map<const VariableNode *, std::shared_ptr<BuilderVariable>> variables; // im sorry it isn't working
    std::unordered_map<const VariableNode *, std::shared_ptr<MultipleLifetime>> lifetimes;

    std::optional<BuilderVariableInfo> findVariable(const VariableNode *node) const;

    Value *get(const BuilderResult &result);
    Value *ref(const BuilderResult &result);

    BuilderResult makeExpressionNounContent(const Node *node);
    BuilderResult makeExpressionNounModifier(const Node *node, const BuilderResult &result);
    BuilderResult makeExpressionNoun(const ExpressionNoun &noun);
    BuilderResult makeExpressionOperation(const ExpressionOperation &operation);
    BuilderResult makeExpressionCombinator(const ExpressionCombinator &combinator);
    BuilderResult makeExpression(const ExpressionResult &result);

    std::optional<BuilderResult> convert(const BuilderResult &result, const Typename &type);

    void makeIf(const IfNode *node);
    void makeFor(const ForNode *node);
    void makeBlock(const BlockNode *node);
    void makeDebug(const DebugNode *node);
    void makeAssign(const AssignNode *node);
    void makeStatement(const StatementNode *node);

    void mergeLifetimes(const BuilderScope &sub);
    void mergePossibleLifetimes(const BuilderScope &sub);

    std::vector<MultipleLifetime *> expand(
        const std::vector<MultipleLifetime *> &lifetime, bool doCopy = false);
    std::vector<MultipleLifetime *> expand(
        std::vector<MultipleLifetime *> lifetime, int32_t depth, bool doCopy = false);

    void join(LifetimeMatches &matches,
        std::vector<MultipleLifetime *> lifetime, const MultipleLifetime &initial);
    void build(const LifetimeMatches &matches,
        const std::vector<MultipleLifetime *> &lifetime, const MultipleLifetime &final);

    BuilderScope(const CodeNode *node, BuilderScope &parent);
    BuilderScope(const CodeNode *node, BuilderFunction &function);

private:
    BuilderScope(const CodeNode *node, BuilderFunction &function, BuilderScope *parent);
};

struct BuilderFunction {
    Builder &builder;

    const FunctionNode *node = nullptr;

    BasicBlock *entryBlock = nullptr;
    BasicBlock *exitBlock = nullptr;

    IRBuilder<> entry;
    IRBuilder<> exit;

    Value *returnValue = nullptr;

    FunctionTypename type;
    Function *function = nullptr;

    BuilderFunction(const FunctionNode *node, Builder &builder);
};

struct Builder {
    Options options;

    LLVMContext context;
    Module module;

    std::unordered_map<const FunctionNode *, std::unique_ptr<BuilderFunction>> functions;

    static const Node *find(const ReferenceNode *node);

    Type *makeStackTypename(const StackTypename &type);
    Type *makeTypename(const Typename &type);

    Builder(RootNode *root, Options options);
};
