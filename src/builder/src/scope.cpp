#include <builder/builder.h>

#include <builder/error.h>
#include <parser/search.h>

#include <parser/scope.h>
#include <parser/assign.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/variable.h>
#include <parser/statement.h>

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
    if (!current)
        return nullptr;

    return result.kind != BuilderResult::Kind::Raw ? current->CreateLoad(result.value) : result.value;
}

Value *BuilderScope::ref(const BuilderResult &result) {
    if (!current)
        return nullptr;

    if (result.kind == BuilderResult::Kind::Raw) {
        Value *ref = function.entry.CreateAlloca(function.builder.makeTypename(result.type));

        current->CreateStore(result.value, ref);

        return ref;
    } else {
        return result.value;
    }
}

void BuilderScope::makeParameters() {
    const FunctionNode *astFunction = function.node;

    auto parameters = astFunction->parameters();

    // Create parameters within scope.
    for (size_t a = 0; a < parameters.size(); a++) {
        const auto *parameterNode = parameters[a];

        Argument *argument = nullptr;

        if (current) {
            argument = function.function->getArg(a);
            argument->setName(parameterNode->name);
        }

        variables[parameterNode] = std::make_shared<BuilderVariable>(parameterNode, argument, *this);
    }
}

BuilderScope::BuilderScope(const Node *node, BuilderFunction &function, BuilderScope *parent, bool doCodeGen)
    : parent(parent), function(function) {
    if (doCodeGen) {
        openingBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
        currentBlock = openingBlock;

        current.emplace(function.builder.context);
        current->SetInsertPoint(currentBlock);
    }

    if (!parent)
        makeParameters();

    if (!node) {
        assert(!doCodeGen);
        return;
    }

    if (node->is(Kind::Expression)) {
        product = makeExpression(node->as<ExpressionNode>());
    } else if (node->is(Kind::Code)) {
        assert(doCodeGen);

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

                default:
                    assert(false);
            }
        }
    } else {
        throw VerifyError(node, "Unsupported BuilderScope node type.");
    }
}

BuilderScope::BuilderScope(const Node *node, BuilderScope &parent, bool doCodeGen)
    : BuilderScope(node, parent.function, &parent, doCodeGen) { }
BuilderScope::BuilderScope(const Node *node, BuilderFunction &function, bool doCodeGen)
    : BuilderScope(node, function, nullptr, doCodeGen) { }
