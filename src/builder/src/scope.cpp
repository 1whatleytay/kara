#include <builder/builder.h>

#include <builder/error.h>
#include <parser/search.h>

#include <parser/assign.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/scope.h>
#include <parser/statement.h>
#include <parser/type.h>
#include <parser/variable.h>

namespace kara::builder {
    builder::Variable *Scope::findVariable(const parser::Variable *node) const {
        const builder::Scope *scope = this;

        while (scope) {
            auto newVariable = scope->variables.find(node);

            if (newVariable != scope->variables.end()) {
                return newVariable->second.get();
            }

            scope = scope->parent;
        }

        return nullptr;
    }

    llvm::Value *Scope::get(const builder::Result &result) {
        if (!current)
            return nullptr;

        return get(result, *current);
    }

    llvm::Value *Scope::get(const builder::Result &result, llvm::IRBuilder<> &irBuilder) const {
        return result.isSet(builder::Result::FlagReference) ? irBuilder.CreateLoad(result.value) : result.value;
    }

    llvm::Value *Scope::ref(const builder::Result &result) {
        if (!current)
            return nullptr;

        return ref(result, *current);
    }

    llvm::Value *Scope::ref(const builder::Result &result, llvm::IRBuilder<> &irBuilder) const {
        if (result.isSet(builder::Result::FlagReference)) {
            return result.value;
        } else {
            assert(function);

            llvm::Value *ref = function->entry.CreateAlloca(function->builder.makeTypename(result.type));

            irBuilder.CreateStore(result.value, ref);

            return ref;
        }
    }

    void Scope::makeParameters() {
        assert(function && function->node->is(parser::Kind::Function));

        auto astFunction = function->node->as<parser::Function>();
        auto parameters = astFunction->parameters();

        // Create parameters within scope.
        for (size_t a = 0; a < parameters.size(); a++) {
            const auto *parameterNode = parameters[a];

            llvm::Argument *argument = nullptr;

            if (current) {
                argument = function->function->getArg(a);
                argument->setName(parameterNode->name);
            }

            variables[parameterNode] = std::make_shared<builder::Variable>(parameterNode, argument, *this);
        }
    }

    void Scope::commit() {
        assert(function);

        assert(exitChainType && lastBlock);

        if (!currentBlock->getTerminator()) {
            exit(ExitPoint::Regular);
        }

        llvm::IRBuilder<> exit(lastBlock);

        auto value = exit.CreateLoad(exitChainType);

        llvm::BasicBlock *pass = function->exitBlock;

        if (parent) {
            pass = llvm::BasicBlock::Create(builder.context, "pass", function->function, lastBlock);
            llvm::IRBuilder<> passBuilder(pass);

            passBuilder.CreateStore(value, parent->exitChainType);
            passBuilder.CreateBr(parent->exitChainBegin);
        }

        auto type = llvm::Type::getInt8Ty(builder.context);
        auto inst = exit.CreateSwitch(value, pass, requiredPoints.size());

        for (ExitPoint point : requiredPoints) {
            auto exitId = static_cast<int8_t>(point);
            auto constant = llvm::ConstantInt::get(type, exitId);

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

    void Scope::exit(ExitPoint point, llvm::BasicBlock *from) {
        assert(exitChainType && exitChainBegin);

        llvm::IRBuilder<> output(from ? from : currentBlock);

        requiredPoints.insert(point);

        auto exitId = static_cast<int8_t>(point);
        output.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(builder.context), exitId), exitChainType);
        output.CreateBr(exitChainBegin);
    }

    Scope::Scope(const hermes::Node *node, builder::Function &function, builder::Scope *parent,
        bool doCodeGen) // NOLINT(misc-no-recursion)
        : parent(parent)
        , builder(function.builder)
        , function(&function)
        , statementContext(*this) {

        llvm::BasicBlock *moveAfter = parent ? parent->lastBlock : function.exitBlock;

        if (doCodeGen) {
            openingBlock = llvm::BasicBlock::Create(function.builder.context, "", function.function, moveAfter);
            currentBlock = openingBlock;

            current.emplace(function.builder.context);
            current->SetInsertPoint(currentBlock);

            if (node && node->is(parser::Kind::Code)) {
                exitChainType = function.entry.CreateAlloca(
                    llvm::Type::getInt8Ty(function.builder.context), nullptr, "exit_type");
                lastBlock
                    = llvm::BasicBlock::Create(function.builder.context, "exit_scope", function.function, moveAfter);
                exitChainBegin = lastBlock;
            }
        }

        if (!parent && !node->is(parser::Kind::Type)) // dont bother building parameters for type
            // destructor functions
            makeParameters();

        if (!node) {
            assert(!doCodeGen);
            return;
        }

        switch (node->is<parser::Kind>()) {
        case parser::Kind::Expression:
            product = makeExpression(node->as<parser::Expression>());

            break;

        case parser::Kind::Code:
            assert(doCodeGen);

            for (const auto &child : node->children) {
                switch (child->is<parser::Kind>()) {
                case parser::Kind::Variable: {
                    auto var = std::make_unique<builder::Variable>(child->as<parser::Variable>(), *this);

                    invokeDestroy(builder::Result {
                        builder::Result::FlagReference | (var->node->isMutable ? builder::Result::FlagMutable : 0),
                        var->value, var->type,
                        &statementContext // safe to put, is reference dw
                    });

                    variables[child->as<parser::Variable>()] = std::move(var);

                    break;
                }

                case parser::Kind::Assign:
                    makeAssign(child->as<parser::Assign>());
                    break;

                case parser::Kind::Statement:
                    makeStatement(child->as<parser::Statement>());
                    continue; // skip statementCommit.commit, makeStatement should do that
                              // at the right time

                case parser::Kind::Block:
                    makeBlock(child->as<parser::Block>());
                    break;

                case parser::Kind::If:
                    makeIf(child->as<parser::If>());
                    break;

                case parser::Kind::For:
                    makeFor(child->as<parser::For>());
                    break;

                case parser::Kind::Expression:
                    makeExpression(child->as<parser::Expression>());
                    break;

                case parser::Kind::Insight: {
                    builder::Scope scope(child->as<parser::Insight>()->expression(), *this, false);
                    assert(scope.product);

                    fmt::print("[INSIGHT, line {}] {}\n",
                        hermes::LineDetails(child->state.text, child->index).lineNumber, toString(scope.product->type));

                    break;
                }

                default:
                    throw;
                }

                statementContext.commit(currentBlock);
            }

            break;

        case parser::Kind::Type: { // create destructor for elements
            assert(function.purpose == builder::Function::Purpose::TypeDestructor); // no mistakes

            auto e = node->as<parser::Type>();

            if (e->isAlias)
                return;

            auto type = builder.makeType(e);
            auto fields = e->fields();

            assert(function.function->arg_size() > 0);

            llvm::Argument *arg = function.function->getArg(0);

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

                auto result = builder::Result(builder::Result::FlagReference, // TODO might need mutable/immutable
                                                                              // versions of implicit destructors
                    current->CreateStructGEP(arg, index), builder.resolveTypename(var->fixedType()),
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

    Scope::Scope(const hermes::Node *node, builder::Scope &parent, bool doCodeGen)
        : Scope(node, parent.function ? *parent.function : throw, &parent, doCodeGen) { }
    Scope::Scope(const hermes::Node *node, builder::Function &function, bool doCodeGen)
        : Scope(node, function, nullptr, doCodeGen) { }
}
