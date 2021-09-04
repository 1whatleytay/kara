#include <builder/builder.h>

#include <builder/error.h>
#include <builder/operations.h>

#include <parser/expression.h>
#include <parser/scope.h>

namespace kara::builder {
    void Scope::makeBlock(const parser::Block *node) {
        assert(current);
        assert(function);

        Scope sub(node->children.front()->as<parser::Code>(), *this);

        switch (node->type) {
        case parser::Block::Type::Regular: {
            current->CreateBr(sub.openingBlock);

            currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);
            current->SetInsertPoint(currentBlock);

            sub.destinations[ExitPoint::Regular] = currentBlock;

            break;
        }

        case parser::Block::Type::Exit: {
            sub.destinations[ExitPoint::Regular] = exitChainBegin;

            // Prohibit Strange Operations, this isn't done in function root scope, IDK
            // what will happen
            sub.destinations[ExitPoint::Break] = nullptr;
            sub.destinations[ExitPoint::Return] = nullptr;
            sub.destinations[ExitPoint::Continue] = nullptr;

            exitChainBegin = sub.openingBlock;

            break;
        }
        }

        sub.commit();
    }

    void Scope::makeIf(const parser::If *node) {
        assert(current);
        assert(function);

        auto context = ops::Context::from(*this);

        std::vector<std::unique_ptr<builder::Scope>> scopes;

        while (node) {
            scopes.push_back(std::make_unique<builder::Scope>(node->children[1]->as<parser::Code>(), *this));
            builder::Scope &sub = *scopes.back();

            currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);

            auto expressionNode = node->children.front()->as<parser::Expression>();
            auto typenameBool = utils::PrimitiveTypename { utils::PrimitiveType::Bool };

            auto conditionResult = ops::expression::make(context, expressionNode);
            auto conditionConverted = ops::makeConvert(context, conditionResult, typenameBool);

            if (!conditionConverted) {
                throw VerifyError(
                    node->children.front().get(), "Condition for if statement must evaluate to true or false.");
            }

            conditionResult = *conditionConverted;

            llvm::Value *condition = ops::get(context, conditionResult);

            current->CreateCondBr(condition, sub.openingBlock, currentBlock);
            current->SetInsertPoint(currentBlock);

            auto onFalseNode = node->onFalse();

            if (node->children.size() == 3) {
                // has else branch
                switch (onFalseNode->is<parser::Kind>()) {
                case parser::Kind::If:
                    node = node->children[2]->as<parser::If>();
                    break;

                case parser::Kind::Code: {
                    scopes.push_back(std::make_unique<builder::Scope>(onFalseNode->as<parser::Code>(), *this));
                    Scope &terminator = *scopes.back();

                    if (!currentBlock->getTerminator())
                        llvm::IRBuilder<>(currentBlock).CreateBr(terminator.openingBlock);

                    currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);
                    current->SetInsertPoint(currentBlock);

                    node = nullptr;

                    break;
                }

                default:
                    throw;
                }
            } else {
                // no branch, all is good
                node = nullptr;
            }
        }

        for (const auto &scope : scopes) {
            if (scope->current) {
                scope->destinations[ExitPoint::Regular] = currentBlock;
                scope->commit();
            }
        }
    }

    void Scope::makeFor(const parser::For *node) {
        assert(current);
        assert(function);

        //        auto context = ops::Context::from(*this);

        const hermes::Node *condition = node->condition();
        auto *code = node->body();

        if (!condition) {
            if (current) { // cleanup is possible using method described in if
                // std::vector<...> scopes
                Scope scope(code, *this, true);

                current->CreateBr(scope.openingBlock);

                currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);
                current->SetInsertPoint(currentBlock);

                scope.destinations[ExitPoint::Break] = currentBlock;
                scope.destinations[ExitPoint::Regular] = scope.openingBlock;
                scope.destinations[ExitPoint::Continue] = scope.openingBlock;
                scope.commit();
            }
        } else if (condition->is(parser::Kind::Expression)) {
            assert(current);

            if (current) {
                Scope check(condition, *this, true);

                auto checkContext = ops::Context::from(check);

                auto typenameBool = utils::PrimitiveTypename { utils::PrimitiveType::Bool };

                assert(check.product);
                auto converted = ops::makeConvert(checkContext, *check.product, typenameBool);

                if (!converted)
                    throw VerifyError(
                        node, "For node must have bool as expression, got {}.", toString(check.product->type));

                Scope scope(code, *this);

                currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);

                current->CreateBr(check.openingBlock);
                current->SetInsertPoint(currentBlock);

                check.current->CreateCondBr(ops::get(checkContext, *check.product), scope.openingBlock, currentBlock);

                scope.destinations[ExitPoint::Break] = currentBlock;
                scope.destinations[ExitPoint::Regular] = check.openingBlock;
                scope.destinations[ExitPoint::Continue] = check.openingBlock;
                scope.commit();
            }
        } else if (condition->is(parser::Kind::ForIn)) {
            throw;
        }
    }
}
