#include <builder/builder.h>

#include <builder/error.h>
#include <parser/search.h>

#include <parser/type.h>
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

    return get(result, *current);
}

Value *BuilderScope::get(const BuilderResult &result, IRBuilder<> &irBuilder) const {
    return result.isSet(BuilderResult::FlagReference) ? irBuilder.CreateLoad(result.value) : result.value;
}

Value *BuilderScope::ref(const BuilderResult &result) {
    if (!current)
        return nullptr;

    return ref(result, *current);
}

Value *BuilderScope::ref(const BuilderResult &result, IRBuilder<> &irBuilder) const {
    if (result.isSet(BuilderResult::FlagReference)) {
        return result.value;
    } else {
        assert(function);

        Value *ref = function->entry.CreateAlloca(function->builder.makeTypename(result.type));

        irBuilder.CreateStore(result.value, ref);

        return ref;
    }
}

void BuilderScope::makeParameters() {
    assert(function && function->node->is(Kind::Function));

    auto astFunction = function->node->as<FunctionNode>();
    auto parameters = astFunction->parameters();

    // Create parameters within scope.
    for (size_t a = 0; a < parameters.size(); a++) {
        const auto *parameterNode = parameters[a];

        Argument *argument = nullptr;

        if (current) {
            argument = function->function->getArg(a);
            argument->setName(parameterNode->name);
        }

        variables[parameterNode] = std::make_shared<BuilderVariable>(parameterNode, argument, *this);
    }
}

void BuilderScope::commit() {
    assert(function);

    assert(exitChainType && lastBlock);

    if (!currentBlock->getTerminator()) {
        exit(ExitPoint::Regular);
    }

    IRBuilder<> exit(lastBlock);

    auto value = exit.CreateLoad(exitChainType);

    BasicBlock *pass = function->exitBlock;

    if (parent) {
        pass = BasicBlock::Create(builder.context, "pass", function->function, lastBlock);
        IRBuilder<> passBuilder(pass);

        passBuilder.CreateStore(value, parent->exitChainType);
        passBuilder.CreateBr(parent->exitChainBegin);
    }

    auto type = Type::getInt8Ty(builder.context);
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
    output.CreateStore(ConstantInt::get(Type::getInt8Ty(builder.context), exitId), exitChainType);
    output.CreateBr(exitChainBegin);
}

BuilderScope::BuilderScope(const Node *node, BuilderFunction &function, BuilderScope *parent, bool doCodeGen) // NOLINT(misc-no-recursion)
    : parent(parent), builder(function.builder), function(&function), statementContext(*this) {

    BasicBlock *moveAfter = parent ? parent->lastBlock : function.exitBlock;

    if (doCodeGen) {
        openingBlock = BasicBlock::Create(function.builder.context, "", function.function, moveAfter);
        currentBlock = openingBlock;

        current.emplace(function.builder.context);
        current->SetInsertPoint(currentBlock);

        if (node && node->is(Kind::Code)) {
            exitChainType = function.entry.CreateAlloca(Type::getInt8Ty(function.builder.context), nullptr, "exit_type");
            lastBlock = BasicBlock::Create(function.builder.context, "exit_scope", function.function, moveAfter);
            exitChainBegin = lastBlock;
        }
    }

    if (!parent && !node->is(Kind::Type)) // dont bother building parameters for type destructor functions
        makeParameters();

    if (!node) {
        assert(!doCodeGen);
        return;
    }

    switch (node->is<Kind>()) {
        case Kind::Expression:
            product = makeExpression(node->as<ExpressionNode>());

            break;

        case Kind::Code:
            assert(doCodeGen);

            for (const auto &child : node->children) {
                switch (child->is<Kind>()) {
                    case Kind::Variable: {
                        auto var = std::make_unique<BuilderVariable>(child->as<VariableNode>(), *this);

                        invokeDestroy(BuilderResult {
                            BuilderResult::FlagReference | (var->node->isMutable ? BuilderResult::FlagMutable : 0),
                            var->value,
                            var->type,
                            &statementContext // safe to put, is reference dw
                        });

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

            break;

        case Kind::Type: { // create destructor for elements
            assert(function.purpose == BuilderFunction::Purpose::TypeDestructor); // no mistakes

            auto e = node->as<TypeNode>();

            if (e->isAlias)
                return;

            auto type = builder.makeType(e);
            auto fields = e->fields();

            assert(function.function->arg_size() > 0);

            Argument *arg = function.function->getArg(0);

            { // sanity checks
                auto underlyingType = arg->getType();
                assert(underlyingType->isPointerTy());

                auto pointeeType = underlyingType->getPointerElementType();
                assert(pointeeType == type->type);
            }

            assert(current);

            for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
                auto var = *it;
                auto index = type->indices.at(var);

                assert(var->hasFixedType);

                auto result = BuilderResult(
                    BuilderResult::FlagReference, // TODO might need mutable/immutable versions of implicit destructors
                    current->CreateStructGEP(arg, index),
                    builder.resolveTypename(var->fixedType()),
                    &statementContext // might as well
                );

                invokeDestroy(result, *current);
            }

            break;
        }

        default:
            throw VerifyError(node, "Unsupported BuilderScope node type.");
    }
}

BuilderScope::BuilderScope(const Node *node, BuilderScope &parent, bool doCodeGen)
    : BuilderScope(node, parent.function ? *parent.function : throw, &parent, doCodeGen) { }
BuilderScope::BuilderScope(const Node *node, BuilderFunction &function, bool doCodeGen)
    : BuilderScope(node, function, nullptr, doCodeGen) { }
