#include <builder/builder.h>

#include <builder/error.h>
#include <builder/search.h>

#include <parser/if.h>
#include <parser/for.h>
#include <parser/block.h>
#include <parser/debug.h>
#include <parser/scope.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/variable.h>
#include <parser/statement.h>
#include <parser/reference.h>

BuilderVariable *BuilderScope::findVariable(const VariableNode *node) const {
    const BuilderScope *scope = this;

    while (scope) {
        auto newVariable = scope->variables.find(node);

        if (newVariable != scope->variables.end()) {
            return newVariable->second.get();
        }

        scope = scope->parent;
    }

    return nullptr;
}

Value *BuilderScope::get(const BuilderResult &result) {
    return result.kind != BuilderResult::Kind::Raw ? current.CreateLoad(result.value) : result.value;
}

Value *BuilderScope::ref(const BuilderResult &result, const Node *node) {
    if (result.kind == BuilderResult::Kind::Raw) {
        Value *ref = function.entry.CreateAlloca(function.builder.makeTypename(result.type, node));

        current.CreateStore(result.value, ref);

        return ref;
    } else {
        return result.value;
    }
}

void BuilderScope::makeParameters() {
    const FunctionNode *astFunction = function.node;
    const Function *llvmFunction = function.function;

    // Create parameters within scope.
    for (size_t a = 0; a < astFunction->parameterCount; a++) {
        const auto *parameterNode = astFunction->children[a]->as<VariableNode>();

        Argument *argument = llvmFunction->getArg(a);
        argument->setName(parameterNode->name);

        variables[parameterNode] = std::make_shared<BuilderVariable>(parameterNode, argument, *this);
    }
}

BuilderScope::BuilderScope(const Node *node, BuilderFunction &function, BuilderScope *parent)
    : parent(parent), function(function), current(*function.builder.context) {
    openingBlock = BasicBlock::Create(*function.builder.context, "", function.function, function.exitBlock);
    currentBlock = openingBlock;

    current.SetInsertPoint(currentBlock);

    if (!parent)
        makeParameters();

    if (node->is(Kind::Expression)) {
        product = makeExpression(node->as<ExpressionNode>());
    } else if (node->is(Kind::Code)) {
        for (const auto &child : node->children) {
            switch (child->is<Kind>()) {
                case Kind::Variable:
                    variables[child->as<VariableNode>()] =
                        std::make_unique<BuilderVariable>(child->as<VariableNode>(), *this);

                    break;

                case Kind::Assign:
                    makeAssign(child->as<AssignNode>());
                    break;

                case Kind::Statement:
                    makeStatement(child->as<StatementNode>());
                    break;

                case Kind::Block:
                    makeBlock(child->as<BlockNode>());
                    break;

                case Kind::If:
                    makeIf(child->as<IfNode>());
                    break;

                case Kind::For:
                    makeFor(child->as<ForNode>());
                    break;

                case Kind::Expression:
                    makeExpression(child->as<ExpressionNode>());
                    break;

                case Kind::Debug:
                    makeDebug(child->as<DebugNode>());
                    break;

                default:
                    assert(false);
            }
        }
    } else {
        throw VerifyError(node, "Unsupported BuilderScope node type.");
    }
}

BuilderScope::BuilderScope(const Node *node, BuilderScope &parent)
    : BuilderScope(node, parent.function, &parent) { }
BuilderScope::BuilderScope(const Node *node, BuilderFunction &function)
    : BuilderScope(node, function, nullptr) { }
