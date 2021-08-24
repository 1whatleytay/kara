#include <builder/builder.h>

#include <builder/error.h>

#include <parser/scope.h>
#include <parser/expression.h>

namespace {
    void br(llvm::BasicBlock *block, llvm::BasicBlock *to) {
        if (!block->getTerminator())
            llvm::IRBuilder<>(block).CreateBr(to);
    }
}

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

                // Prohibit Strange Operations, this isn't done in function root scope, idk what will happen
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

        std::vector<std::unique_ptr<builder::Scope>> scopes;

        while (node) {
            scopes.push_back(std::make_unique<builder::Scope>(node->children[1]->as<parser::Code>(), *this));
            builder::Scope &sub = *scopes.back();

            currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);

            builder::Result conditionResult = makeExpression(node->children.front()->as<parser::Expression>());
            std::optional<builder::Result> conditionConverted =
                convert(conditionResult, utils::PrimitiveTypename { utils::PrimitiveType::Bool });

            if (!conditionConverted) {
                throw VerifyError(node->children.front().get(),
                    "Condition for if statement must evaluate to true or false.");
            }

            conditionResult = *conditionConverted;

            llvm::Value *condition = get(conditionResult);

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

                        br(currentBlock, terminator.openingBlock);

                        currentBlock = llvm::BasicBlock::Create(
                            builder.context, "", function->function, lastBlock);
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

        const hermes::Node *condition = node->condition();
        auto *code = node->body();

        if (!condition) {
            if (current) { // cleanup is possible using method described in if std::vector<...> scopes
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

                assert(check.product);
                auto converted = convert(*check.product, utils::PrimitiveTypename { utils::PrimitiveType::Bool });

                if (!converted)
                    throw VerifyError(node, "For node must have bool as expression, got {}.", toString(check.product->type));

                Scope scope(code, *this);

                currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);

                current->CreateBr(check.openingBlock);
                current->SetInsertPoint(currentBlock);

                check.current->CreateCondBr(check.get(*check.product), scope.openingBlock, currentBlock);

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
