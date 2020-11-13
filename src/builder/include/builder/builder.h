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

struct Scope {
    BasicBlock *entry = nullptr;
    BasicBlock *exit = nullptr;

    // mutable
    BasicBlock *current = nullptr;

    Typename returnType = TypenameNode::nothing;
    Value *returnValue = nullptr;
};

struct Result {
    enum class Kind {
        Raw,
        Reference
    };

    Kind kind = Kind::Raw;
    Value *value = nullptr;
    Typename type;

    Value *get(IRBuilder<> &builder) const;
};

struct Variable {
    const VariableNode *node = nullptr;

    Typename type;
    Value *value = nullptr;

    bool isMutable = false;
};

struct Callable {
    std::shared_ptr<FunctionTypename> type;

    Function *value = nullptr;
};

struct Builder {
    LLVMContext context;
    Module module;

    std::unordered_map<const VariableNode *, Variable> variables;
    std::unordered_map<const FunctionNode *, Callable> functions;

    static const Node *find(const ReferenceNode *node);

    Result makeExpressionNounContent(const Node *node, const Scope &scope);
    Result makeExpressionNounModifier(const Node *node, const Result &result, const Scope &scope);
    Result makeExpressionNoun(const ExpressionNoun &noun, const Scope &scope);
    Result makeExpressionOperation(const ExpressionOperation &operation, const Scope &scope);
    Result makeExpressionCombinator(const ExpressionCombinator &combinator, const Scope &scope);
    Result makeExpression(const ExpressionResult &result, const Scope &scope);

    Type *makeStackTypename(const StackTypename &type);
    Type *makeTypename(const Typename &type);

    void makeAssign(const AssignNode *node, Scope &scope);
    void makeStatement(const StatementNode *node, const Scope &scope);

    Variable makeLocalVariable(const VariableNode *node, Scope &scope);
    Variable makeParameterVariable(const VariableNode *node, Value *value, Scope &scope);
    Variable makeVariable(const VariableNode *node, const Scope &scope);

    void makeCode(const CodeNode *node, Scope &scope);

    static FunctionTypename makeFunctionTypenameBase(const FunctionNode *node);
    Callable makeFunction(const FunctionNode *node);

    Builder(RootNode *root, const Options &options);
};
