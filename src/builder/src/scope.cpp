#include <builder/builder.h>

#include <builder/error.h>
#include <builder/operations.h>

#include <parser/assign.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/scope.h>
#include <parser/search.h>
#include <parser/statement.h>
#include <parser/type.h>
#include <parser/variable.h>

namespace kara::builder {
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

            cache->variables[parameterNode] = std::make_unique<builder::Variable>(parameterNode, argument, *this);
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

    // Moving out to stop confusing the analysis
    Cache *createCache(builder::Function &function, builder::Scope *parent) {
        return parent ? parent->cache->create() : function.cache.create();
    }

    Scope::Scope(const hermes::Node *node, builder::Function &function, builder::Scope *parent,
        bool doCodeGen) // NOLINT(misc-no-recursion)
        : parent(parent)
        , builder(function.builder)
        , function(&function)
        , cache(createCache(function, parent)) {

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

        if (!parent && !node->is(parser::Kind::Type)) // don't bother building parameters for type
            // destructor functions
            makeParameters();

        if (!node) {
            assert(!doCodeGen);
            return;
        }

        auto context = ops::Context::from(*this);

        switch (node->is<parser::Kind>()) {
        case parser::Kind::Expression:
            product = ops::expression::make(context, node->as<parser::Expression>());

            break;

        case parser::Kind::Code:
            assert(doCodeGen);

            for (const auto &child : node->children) {
                switch (child->is<parser::Kind>()) {
                case parser::Kind::Variable: {
                    auto var = std::make_unique<builder::Variable>(child->as<parser::Variable>(), *this);

                    auto result = builder::Result {
                        builder::Result::FlagReference | (var->node->isMutable ? builder::Result::FlagMutable : 0),
                        var->value, var->type,
                        &accumulator, // safe to put, is reference dw
                    };

                    ops::makeInvokeDestroy(context, result);

                    cache->variables[child->as<parser::Variable>()] = std::move(var);

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
                    ops::expression::make(context, child->as<parser::Expression>());
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

                if (context.ir)
                    accumulator.commit(context.builder, *context.ir);
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

                auto elementType = underlyingType->getPointerElementType();
                assert(elementType == type->type);
            }

            assert(current);

            for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
                auto var = *it;
                auto index = type->indices.at(var);

                assert(var->hasFixedType);

                // TODO might need mutable/immutable versions of implicit destructors
                auto result = builder::Result {
                    builder::Result::FlagReference, current->CreateStructGEP(arg, index),
                    builder.resolveTypename(var->fixedType()),
                    &accumulator, // might as well
                };

                ops::makeInvokeDestroy(context, result);
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
