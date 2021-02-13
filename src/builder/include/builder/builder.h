#pragma once

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

    BuilderResult(Kind kind, Value *value, Typename type);
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

    IRBuilder<> current;

    // separate for now... for data efficiency - use findVariable function
    std::unordered_map<const VariableNode *, std::shared_ptr<BuilderVariable>> variables;

    BuilderVariable *findVariable(const VariableNode *node) const;
    // Node for search scope.
    std::optional<BuilderResult> convert(const BuilderResult &result, const Typename &type, const Node *node);

    Value *get(const BuilderResult &result);
    Value *ref(const BuilderResult &result, const Node *node);

    BuilderResult makeExpressionNounContent(const Node *node);
    BuilderResult makeExpressionNounModifier(const Node *node, const BuilderResult &result);
    BuilderResult makeExpressionNoun(const ExpressionNoun &noun);
    BuilderResult makeExpressionOperation(const ExpressionOperation &operation);
    BuilderResult makeExpressionCombinator(const ExpressionCombinator &combinator);
    BuilderResult makeExpression(const ExpressionResult &result);

    void makeIf(const IfNode *node);
    void makeFor(const ForNode *node);
    void makeBlock(const BlockNode *node);
    void makeDebug(const DebugNode *node);
    void makeAssign(const AssignNode *node);
    void makeStatement(const StatementNode *node);

    BuilderScope(const CodeNode *node, BuilderScope &parent);
    BuilderScope(const CodeNode *node, BuilderFunction &function);

private:
    BuilderScope(const CodeNode *node, BuilderFunction &function, BuilderScope *parent);
};

struct BuilderType {
    StructType *type = nullptr;

    std::unordered_map<const VariableNode *, size_t> indices;

    BuilderType(const TypeNode *node, Builder &builder);
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

    std::unordered_map<const TypeNode *, std::unique_ptr<BuilderType>> types;
    std::unordered_map<const FunctionNode *, std::unique_ptr<BuilderFunction>> functions;

    BuilderType *makeType(const TypeNode *node);
    BuilderFunction *makeFunction(const FunctionNode *node);

    static const Node *find(const ReferenceNode *node);
    static const TypeNode *find(const StackTypename &type, const Node *node);

    Type *makeBuiltinTypename(const StackTypename &type);

    // Node needs to be passed to get a sense of scope.
    Type *makeStackTypename(const StackTypename &type, const Node *node);
    Type *makeTypename(const Typename &type, const Node *node);

    Builder(RootNode *root, Options options);
};
