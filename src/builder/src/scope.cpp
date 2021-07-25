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

void BuilderScope::commit() {
    assert(exitChainType && lastBlock);

    if (!currentBlock->getTerminator()) {
        exit(ExitPoint::Regular);
    }

    IRBuilder<> exit(lastBlock);

    auto value = exit.CreateLoad(exitChainType);

    BasicBlock *pass = function.exitBlock;

    if (parent) {
        pass = BasicBlock::Create(function.builder.context, "pass", function.function, lastBlock);
        IRBuilder<> passBuilder(pass);

        passBuilder.CreateStore(value, parent->exitChainType);
        passBuilder.CreateBr(parent->exitChainBegin);
    }

    auto type = Type::getInt8Ty(function.builder.context);
    auto inst = exit.CreateSwitch(value, pass, requiredPoints.size());

    for (ExitPoint point : requiredPoints) {
        auto exitId = static_cast<int8_t>(point);
        auto constant = ConstantInt::get(type, exitId);

        auto iterator = destinations.find(point);

        if (iterator != destinations.end()) {
            assert(iterator->second); // break/continue/return is just not allowed here

            inst->addCase(constant, iterator->second);
        } else if (parent) {
            parent->requiredPoints.insert(point);
        } else {
            throw;
        }
    }
}

void BuilderScope::exit(ExitPoint point, BasicBlock *from) {
    assert(exitChainType && exitChainBegin);

    IRBuilder<> output(from ? from : currentBlock);

    requiredPoints.insert(point);

    auto exitId = static_cast<int8_t>(point);
    output.CreateStore(ConstantInt::get(Type::getInt8Ty(function.builder.context), exitId), exitChainType);
    output.CreateBr(exitChainBegin);
}

BuilderScope::BuilderScope(const Node *node, BuilderFunction &function, BuilderScope *parent, bool doCodeGen) // NOLINT(misc-no-recursion)
    : parent(parent), function(function), statementContext(*this, doCodeGen) {

    BasicBlock *moveAfter = parent ? parent->lastBlock : function.exitBlock;

    if (doCodeGen) {
        openingBlock = BasicBlock::Create(function.builder.context, "", function.function, moveAfter);
        currentBlock = openingBlock;

        current.emplace(function.builder.context);
        current->SetInsertPoint(currentBlock);

        if (node->is(Kind::Code)) {
            exitChainType = function.entry.CreateAlloca(Type::getInt8Ty(function.builder.context), nullptr, "exit_type");
            lastBlock = BasicBlock::Create(function.builder.context, "exit_scope", function.function, moveAfter);
            exitChainBegin = lastBlock;
        }
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
                case Kind::Variable: {
                    auto var = std::make_unique<BuilderVariable>(child->as<VariableNode>(), *this);

                    if (!std::holds_alternative<ReferenceTypename>(var->type)) {
                        invokeDestroy(BuilderResult {
                            BuilderResult::Kind::Reference,
                            var->value,
                            var->type,
                            &statementContext
                        });
                    }

                    variables[child->as<VariableNode>()] = std::move(var);

                    break;
                }

                case Kind::Assign:
                    makeAssign(child->as<AssignNode>());
                    break;

                case Kind::Statement:
                    makeStatement(child->as<StatementNode>());
                    continue; // skip statementCommit.commit, makeStatement should do that at the right time

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

                case Kind::Insight: {
                    BuilderScope scope(child->as<InsightNode>()->expression(), *this, false);
                    assert(scope.product);

                    fmt::print("[INSIGHT, line {}] {}\n",
                        LineDetails(child->state.text, child->index).lineNumber, toString(scope.product->type));

                    break;
                }

                default:
                    throw;
            }

            statementContext.commit(currentBlock);
        }
    } else {
        throw VerifyError(node, "Unsupported BuilderScope node type.");
    }
}

BuilderScope::BuilderScope(const Node *node, BuilderScope &parent, bool doCodeGen)
    : BuilderScope(node, parent.function, &parent, doCodeGen) { }
BuilderScope::BuilderScope(const Node *node, BuilderFunction &function, bool doCodeGen)
    : BuilderScope(node, function, nullptr, doCodeGen) { }
