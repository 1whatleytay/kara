#pragma once

#include <builder/lifetime.h>

#include <options/options.h>

#include <parser/root.h>
#include <parser/typename.h>
#include <parser/expression.h>

#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

#include <unordered_map>

using namespace llvm;

struct CodeNode;
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
        Reference
    };

    Kind kind = Kind::Raw;
    Value *value = nullptr;
    Typename type;

    int32_t lifetimeDepth = 0;
    std::shared_ptr<MultipleLifetime> lifetime;

    BuilderResult(Kind kind, Value *value, Typename type,
        int32_t lifetimeDepth, std::shared_ptr<MultipleLifetime> lifetime);
};

struct BuilderVariable {
    BuilderFunction &function;

    const VariableNode *node = nullptr;

    Value *value = nullptr;

    int64_t lifetimeLevel = 0;
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

    Typename returnType = TypenameNode::nothing;

    int64_t lifetimeLevel = 0;

    // separate for now... for data efficiency - use findVariable/findLifetime functions
    std::unordered_map<const VariableNode *, std::unique_ptr<BuilderVariable>> variables;
    std::unordered_map<const VariableNode *, std::shared_ptr<MultipleLifetime>> lifetimes;

    std::optional<BuilderVariableInfo> findVariable(const VariableNode *node) const;

    Value *get(const BuilderResult &result);

    BuilderResult makeExpressionNounContent(const Node *node);
    BuilderResult makeExpressionNounModifier(const Node *node, const BuilderResult &result);
    BuilderResult makeExpressionNoun(const ExpressionNoun &noun);
    BuilderResult makeExpressionOperation(const ExpressionOperation &operation);
    BuilderResult makeExpressionCombinator(const ExpressionCombinator &combinator);
    BuilderResult makeExpression(const ExpressionResult &result);

    void makeAssign(const AssignNode *node);
    void makeStatement(const StatementNode *node);

    std::vector<MultipleLifetime *> expand(MultipleLifetime &lifetime, int32_t depth, bool doCopy = false);

    BuilderScope(const CodeNode *node, BuilderScope &parent);
    BuilderScope(const CodeNode *node, BuilderFunction &function);

private:
    // Abstract out of constructor.
    void build(const CodeNode *node);
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
    LLVMContext context;
    Module module;

    std::unordered_map<const FunctionNode *, std::unique_ptr<BuilderFunction>> functions;

    static const Node *find(const ReferenceNode *node);

    Type *makeStackTypename(const StackTypename &type);
    Type *makeTypename(const Typename &type);

    Builder(RootNode *root, const Options &options);
};
