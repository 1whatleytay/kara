#include <builder/builder.h>

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

std::optional<BuilderVariableInfo> BuilderScope::findVariable(const VariableNode *node) const {
    const BuilderScope *scope = this;

    // yeah double pointer
    // i'm just worried that a BuilderResult might outlive a BuilderVariable for some reason
    const std::shared_ptr<MultipleLifetime> *lifetime = nullptr;

    while (scope) {
        if (!lifetime) {
            auto newLifetime = scope->lifetimes.find(node);

            if (newLifetime != scope->lifetimes.end()) {
                lifetime = &newLifetime->second; // god i hope this is a proper reference
            }
        }

        auto newVariable = scope->variables.find(node);

        if (newVariable != scope->variables.end()) {
            if (!lifetime)
                throw std::runtime_error("Missing lifetime.");

            if (!*lifetime)
                throw std::runtime_error("Incorrect lifetime for variable.");

            return BuilderVariableInfo { *newVariable->second, *lifetime };
        }

        scope = scope->parent;
    }

    return std::nullopt;
}

Value *BuilderScope::get(const BuilderResult &result) {
    return result.kind != BuilderResult::Kind::Raw ? current.CreateLoad(result.value) : result.value;
}

Value *BuilderScope::ref(const BuilderResult &result) {
    if (result.kind == BuilderResult::Kind::Raw) {
        Value *ref = current.CreateAlloca(function.builder.makeTypename(result.type));

        current.CreateStore(result.value, ref);

        return ref;
    } else {
        return result.value;
    }
}

BuilderScope::BuilderScope(const CodeNode *node, BuilderFunction &function, BuilderScope *parent)
    : parent(parent), function(function), current(function.builder.context) {
    openingBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
    currentBlock = openingBlock;

    current.SetInsertPoint(currentBlock);

    if (!parent) {
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
                makeExpression(child->as<ExpressionNode>()->result);
                break;

            case Kind::Debug:
                makeDebug(child->as<DebugNode>());
                break;

            default:
                assert(false);
        }
    }
}

BuilderScope::BuilderScope(const CodeNode *node, BuilderScope &parent)
    : BuilderScope(node, parent.function, &parent) { }
BuilderScope::BuilderScope(const CodeNode *node, BuilderFunction &function)
    : BuilderScope(node, function, nullptr) { }
