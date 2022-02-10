#include <builder/operations.h>

#include <parser/assign.h>
#include <parser/expression.h>
#include <parser/scope.h>
#include <parser/statement.h>
#include <parser/variable.h>

#include <cassert>

namespace kara::builder::ops::statements {
    void exit(const Context &context, ExitPoint point) {
        assert(context.ir);
        assert(context.exitInfo);

        auto exitId = static_cast<int8_t>(point);
        auto llvmExitId = llvm::ConstantInt::get(llvm::Type::getInt8Ty(context.builder.context), exitId);

        context.ir->CreateStore(llvmExitId, context.exitInfo->exitChainType);
        context.ir->CreateBr(context.exitInfo->exitChainBegin);
    }

    namespace {
        llvm::BasicBlock *recurseIf(const Context &context, const parser::If *node, llvm::BasicBlock *next) {
            auto conditionNode = node->condition();

            auto onTrue = node->onTrue();
            auto onFalse = node->onFalse();

            llvm::BasicBlock *falseNext = next;

            if (onFalse) {
                switch (onFalse->is<parser::Kind>()) {
                case parser::Kind::Code:
                    falseNext = ops::statements::makeScope(
                        context, onFalse->as<parser::Code>(), { { ExitPoint::Regular, next } });

                    break;

                case parser::Kind::If:
                    falseNext = recurseIf(context, node->as<parser::If>(), next);
                    break;

                default:
                    throw;
                }
            }

            auto trueScope = ops::statements::makeScope(context, onTrue, { { ExitPoint::Regular, next } });

            auto conditionBlock
                = llvm::BasicBlock::Create(context.builder.context, "check", context.function->function, trueScope);

            llvm::IRBuilder<> conditionBuilder(conditionBlock);
            auto conditionContext = context.move(&conditionBuilder);

            auto conditionResult = ops::expression::make(conditionContext, conditionNode);

            auto converted = ops::makeConvert(conditionContext, conditionResult, from(utils::PrimitiveType::Bool));
            if (!converted) {
                throw VerifyError(
                    node->children.front().get(), "Condition for if statement must evaluate to a bool.");
            }

            conditionBuilder.CreateCondBr(ops::get(conditionContext, *converted), trueScope, falseNext);

            return conditionBlock;
        }
    }

    void makeIf(const Context &context, const parser::If *node) {
        assert(context.ir);
        assert(context.function);

        auto after = context.ir->GetInsertBlock()->getNextNode();

        auto nextBlock = llvm::BasicBlock::Create(context.builder.context, "", context.function->function, after);

        auto condition = recurseIf(context, node, nextBlock);
        context.ir->CreateBr(condition);

        context.ir->SetInsertPoint(nextBlock);
    }

    void makeFor(const Context &context, const parser::For *node) {
        assert(context.ir);
        assert(context.function);

        const hermes::Node *condition = node->condition();
        auto *code = node->body();

        if (!condition) {
            auto after = context.ir->GetInsertBlock()->getNextNode();

            auto jumpBlock
                = llvm::BasicBlock::Create(context.builder.context, "jump", context.function->function, after);

            auto nextBlock = llvm::BasicBlock::Create(context.builder.context, "", context.function->function, after);

            // std::vector<...> scopes
            auto scope = ops::statements::makeScope(context, code,
                {
                    { ExitPoint::Break, nextBlock },
                    { ExitPoint::Regular, jumpBlock },
                    { ExitPoint::Continue, jumpBlock },
                });

            context.ir->CreateBr(scope);
            context.ir->SetInsertPoint(nextBlock);

            llvm::IRBuilder<> jumpBuilder(jumpBlock);
            jumpBuilder.CreateBr(scope);
        } else if (condition->is(parser::Kind::Expression)) {
            auto after = context.ir->GetInsertBlock()->getNextNode();

            auto jumpBlock
                = llvm::BasicBlock::Create(context.builder.context, "jump", context.function->function, after);

            auto nextBlock = llvm::BasicBlock::Create(context.builder.context, "", context.function->function, after);

            llvm::IRBuilder<> jumpBuilder(jumpBlock);
            auto jumpContext = context.move(&jumpBuilder);

            auto expressionNode = condition->as<parser::Expression>();

            auto conditionResult = ops::expression::make(jumpContext, expressionNode);

            auto converted = ops::makeConvert(jumpContext, conditionResult, from(utils::PrimitiveType::Bool));

            if (!converted) {
                throw VerifyError(
                    node, "For node must have bool as expression, got {}.", toString(conditionResult.type));
            }

            auto scope = ops::statements::makeScope(jumpContext, code,
                {
                    { ExitPoint::Break, nextBlock },
                    { ExitPoint::Regular, jumpBlock },
                    { ExitPoint::Continue, jumpBlock },
                });

            context.ir->CreateBr(jumpBlock);
            context.ir->SetInsertPoint(nextBlock);

            jumpBuilder.CreateCondBr(ops::get(jumpContext, conditionResult), scope, nextBlock);
        } else if (condition->is(parser::Kind::ForIn)) {
            throw;
        }
    }

    void makeBlock(const Context &context, const parser::Block *node) {
        assert(context.ir);
        assert(context.function);

        auto code = node->children.front()->as<parser::Code>();

        switch (node->type) {
        case parser::Block::Type::Regular: {
            auto after = context.ir->GetInsertBlock()->getNextNode();

            auto nextBlock = llvm::BasicBlock::Create(context.builder.context, "", context.function->function, after);

            auto scope = ops::statements::makeScope(context, code, { { ExitPoint::Regular, nextBlock } });

            context.ir->CreateBr(scope);
            context.ir->SetInsertPoint(nextBlock);

            //            sub.destinations[ExitPoint::Regular] = currentBlock;

            break;
        }

        case parser::Block::Type::Exit: {
            assert(context.exitInfo);

            // Prohibit Strange Operations, by setting to nullptr,
            // this isn't done in function root scope, IDK what will happen
            auto scope = ops::statements::makeScope(context, code,
                {
                    { ExitPoint::Regular, context.exitInfo->exitChainBegin },
                    { ExitPoint::Break, nullptr },
                    { ExitPoint::Return, nullptr },
                    { ExitPoint::Continue, nullptr },
                });

            context.exitInfo->exitChainBegin = scope;

            break;
        }
        }
    }

    void makeAssign(const Context &context, const parser::Assign *node) {
        auto destination = ops::expression::make(context, node->children.front()->as<parser::Expression>());

        auto sourceRaw = ops::expression::make(context, node->children.back()->as<parser::Expression>());
        auto sourceConverted = ops::makeConvert(context, sourceRaw, destination.type);

        if (!sourceConverted) {
            throw VerifyError(node, "Assignment of type {} to {} is not allowed.", toString(sourceRaw.type),
                toString(destination.type));
        }

        auto source = std::move(*sourceConverted);

        if (!destination.isSet(builder::Result::FlagReference) || !destination.isSet(builder::Result::FlagMutable)) {
            throw VerifyError(node, "Left side of assign expression must be a mutable variable.");
        }

        if (context.ir) {
            llvm::Value *result;

            try {
                if (node->op == parser::Assign::Operator::Assign) {
                    result = ops::get(context, ops::makePass(context, source));
                } else {
                    auto operation = ([&]() {
                        switch (node->op) {
                        case parser::Assign::Operator::Plus:
                            return ops::binary::makeAdd(context, destination, source);
                        case parser::Assign::Operator::Minus:
                            return ops::binary::makeSub(context, destination, source);
                        case parser::Assign::Operator::Multiply:
                            return ops::binary::makeMul(context, destination, source);
                        case parser::Assign::Operator::Divide:
                            return ops::binary::makeDiv(context, destination, source);
                        case parser::Assign::Operator::Modulo:
                            return ops::binary::makeMod(context, destination, source);
                        default:
                            throw std::runtime_error("Unimplemented assign node operator.");
                        }
                    })();

                    result = ops::get(context, ops::makePass(context, operation));
                }
            } catch (const std::runtime_error &e) { throw VerifyError(node, "{}", e.what()); }

            context.ir->CreateStore(result, destination.value);
        }
    }

    void makeStatement(const Context &context, const parser::Statement *node) {
        assert(context.ir);

        auto nothing = from(utils::PrimitiveType::Nothing);

        switch (node->op) {
        case parser::Statement::Operation::Return: {
            assert(context.function);

            auto &returnType = *context.function->type.returnType;

            if (node->children.empty()) {
                if (*context.function->type.returnType != nothing) {
                    throw VerifyError(node,
                        "Method is of type {} but return statement does not "
                        "return anything",
                        toString(*context.function->type.returnType));
                }
            } else {
                if (!node->children.empty() && returnType == nothing) {
                    throw VerifyError(node,
                        "Method does not have a return type but return "
                        "statement returns value.");
                }

                // lambda :S

                auto expressionNode = node->children.front()->as<parser::Expression>();

                auto resultRaw = ops::expression::make(context, expressionNode);
                auto resultConverted = ops::makeConvert(context, resultRaw, returnType);

                if (!resultConverted.has_value()) {
                    throw VerifyError(node, "Cannot return {} from a function that returns {}.",
                        toString(resultRaw.type), toString(returnType));
                }

                builder::Result result = ops::makePass(context, *resultConverted);

                context.ir->CreateStore(ops::get(context, result), context.function->returnValue);
            }

            // might want to make ops::makeAccumulatorCommit(const Context &, const builder::Accumulator &)
            if (context.ir && context.accumulator)
                context.accumulator->commit(context);

            // TODO: something needs to be done about this insert block... this is a temp solution
            ops::statements::exit(context, ExitPoint::Return);

            break;
        }

        case parser::Statement::Operation::Break:
            ops::statements::exit(context, ExitPoint::Break);
            break;

        case parser::Statement::Operation::Continue:
            ops::statements::exit(context, ExitPoint::Continue);
            break;

        default:
            throw;
        }
    }

    llvm::BasicBlock *makeScope(const Context &parent, const parser::Code *node, const Destinations &destinations) {
        assert(node);

        llvm::BasicBlock *openingBlock = nullptr;

        std::optional<llvm::IRBuilder<>> current;

        ExitInfo exitInfo;

        if (parent.ir) {
            assert(parent.function);

            auto after = parent.ir->GetInsertBlock()->getNextNode();

            openingBlock = llvm::BasicBlock::Create(parent.builder.context, "", parent.function->function, after);

            current.emplace(openingBlock);

            exitInfo.exitChainType = parent.function->entry.CreateAlloca(parent.ir->getInt8Ty(), nullptr, "exit_type");
            exitInfo.exitChainBegin
                = llvm::BasicBlock::Create(parent.builder.context, "exit_scope", parent.function->function, after);
            exitInfo.exitChainEnd = exitInfo.exitChainBegin;
        }

        Accumulator accumulator;
        Cache *cache = parent.cache->create();

        Context context = {
            parent.builder,
            &accumulator,

            current ? &*current : nullptr,
            cache,
            parent.function,

            &exitInfo,
        };

        for (const auto &child : node->children) {
            try {
                switch (child->is<parser::Kind>()) {
                case parser::Kind::Variable: {
                    auto var = std::make_unique<builder::Variable>(child->as<parser::Variable>(), context);

                    llvm::IRBuilder<> exitBuilder(exitInfo.exitChainBegin, exitInfo.exitChainBegin->begin());

                    ops::makeDestroy(context.move(&exitBuilder), var->value, var->type);

                    cache->variables[child->as<parser::Variable>()] = std::move(var);

                    break;
                }

                case parser::Kind::Assign:
                    ops::statements::makeAssign(context, child->as<parser::Assign>());
                    break;

                case parser::Kind::Statement:
                    ops::statements::makeStatement(context, child->as<parser::Statement>());
                    continue; // skip statementCommit.commit, makeStatement should do that
                              // at the right time

                case parser::Kind::Block:
                    ops::statements::makeBlock(context, child->as<parser::Block>());
                    break;

                case parser::Kind::If:
                    ops::statements::makeIf(context, child->as<parser::If>());
                    break;

                case parser::Kind::For:
                    ops::statements::makeFor(context, child->as<parser::For>());
                    break;

                case parser::Kind::Expression:
                    ops::expression::make(context, child->as<parser::Expression>());
                    break;

                case parser::Kind::Insight: {
                    auto result = ops::expression::make(context.noIR(), child->as<parser::Insight>()->expression());

                    fmt::print("[INSIGHT, line {}] {}\n",
                        hermes::LineDetails(child->state.text, child->index).lineNumber, toString(result.type));

                    break;
                }

                default:
                    throw;
                }
            } catch (const std::runtime_error &e) {
                throw VerifyError(child.get(), "{}", e.what());
            }

            if (context.ir)
                accumulator.commit(context);
        }

        assert(context.function);

        assert(context.ir);
        assert(context.exitInfo);

        if (!context.ir->GetInsertBlock()->getTerminator()) {
            ops::statements::exit(context, ExitPoint::Regular);
        }

        // commit
        if (context.ir) {
            llvm::IRBuilder<> exit(exitInfo.exitChainEnd);

            auto i8 = llvm::Type::getInt8Ty(context.builder.context);
            auto value = exit.CreateLoad(i8, exitInfo.exitChainType);

            llvm::BasicBlock *pass = context.function->exitBlock;

            if (parent.exitInfo) { // exitInfo is set given function doesn't create it? can I abstract this further?
                pass = llvm::BasicBlock::Create(
                    context.builder.context, "pass", context.function->function, exitInfo.exitChainEnd->getNextNode());
                llvm::IRBuilder<> passBuilder(pass);

                passBuilder.CreateStore(value, parent.exitInfo->exitChainType);
                passBuilder.CreateBr(parent.exitInfo->exitChainBegin);
            }

            auto maxCases = 4;

            auto type = llvm::Type::getInt8Ty(context.builder.context);
            auto inst = exit.CreateSwitch(value, pass, maxCases);

            for (const auto &pair : destinations) {
                auto exitId = static_cast<int8_t>(pair.first);
                auto constant = llvm::ConstantInt::get(type, exitId);

                if (pair.second)
                    inst->addCase(constant, pair.second);
            }
        }

        return openingBlock;
    }
}