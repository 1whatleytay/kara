#include <builder/builder.h>

#include <builder/error.h>

#include <parser/scope.h>

namespace {
    void br(BasicBlock *block, BasicBlock *to) {
        if (!block->getTerminator())
            IRBuilder<>(block).CreateBr(to);
    }
}

void BuilderScope::makeBlock(const BlockNode *node) {
    assert(current);
    assert(function);

    BuilderScope sub(node->children.front()->as<CodeNode>(), *this);

    switch (node->type) {
        case BlockNode::Type::Regular: {
            current->CreateBr(sub.openingBlock);

            currentBlock = BasicBlock::Create(builder.context, "", function->function, lastBlock);
            current->SetInsertPoint(currentBlock);

            sub.destinations[ExitPoint::Regular] = currentBlock;

            break;
        }

        case BlockNode::Type::Exit: {
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

void BuilderScope::makeIf(const IfNode *node) {
    assert(current);
    assert(function);

    std::vector<std::unique_ptr<BuilderScope>> scopes;

    while (node) {
        scopes.push_back(std::make_unique<BuilderScope>(node->children[1]->as<CodeNode>(), *this));
        BuilderScope &sub = *scopes.back();

        currentBlock = BasicBlock::Create(builder.context, "", function->function, lastBlock);

        BuilderResult conditionResult = makeExpression(node->children.front()->as<ExpressionNode>());
        std::optional<BuilderResult> conditionConverted =
            convert(conditionResult, PrimitiveTypename { PrimitiveType::Bool });

        if (!conditionConverted) {
            throw VerifyError(node->children.front().get(),
                "Condition for if statement must evaluate to true or false.");
        }

        conditionResult = *conditionConverted;

        Value *condition = get(conditionResult);

        current->CreateCondBr(condition, sub.openingBlock, currentBlock);
        current->SetInsertPoint(currentBlock);

        if (node->children.size() == 3) {
            // has else branch
            switch (node->children[2]->is<Kind>()) {
                case Kind::If:
                    node = node->children[2]->as<IfNode>();
                    break;

                case Kind::Code: {
                    scopes.push_back(std::make_unique<BuilderScope>(node->children[2]->as<CodeNode>(), *this));
                    BuilderScope &terminator = *scopes.back();

                    br(currentBlock, terminator.openingBlock);

                    currentBlock = BasicBlock::Create(
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

void BuilderScope::makeFor(const ForNode *node) {
    assert(current);
    assert(function);

    const Node *condition = node->condition();
    auto *code = node->body();

    if (!condition) {
        if (current) { // cleanup is possible using method described in if std::vector<...> scopes
            BuilderScope scope(code, *this, true);

            current->CreateBr(scope.openingBlock);

            currentBlock = BasicBlock::Create(builder.context, "", function->function, lastBlock);
            current->SetInsertPoint(currentBlock);

            scope.destinations[ExitPoint::Break] = currentBlock;
            scope.destinations[ExitPoint::Regular] = scope.openingBlock;
            scope.destinations[ExitPoint::Continue] = scope.openingBlock;
            scope.commit();
        }
    } else if (condition->is(Kind::Expression)) {
        assert(current);

        if (current) {
//            BasicBlock *entry = BasicBlock::Create(
//                function.builder.context, "loop_entry", function.function, function.exitBlock);
//
//            BasicBlock *exit = BasicBlock::Create(
//                function.builder.context, "loop_exit", function.function, function.exitBlock);

            BuilderScope check(condition, *this, true);

            assert(check.product);
            auto converted = convert(*check.product, PrimitiveTypename { PrimitiveType::Bool });

            if (!converted)
                throw VerifyError(node, "For node must have bool as expression, got {}.", toString(check.product->type));

            BuilderScope scope(code, *this);

            currentBlock = BasicBlock::Create(builder.context, "", function->function, lastBlock);

            current->CreateBr(check.openingBlock);
            current->SetInsertPoint(currentBlock);

            check.current->CreateCondBr(check.get(*check.product), scope.openingBlock, currentBlock);

            scope.destinations[ExitPoint::Break] = currentBlock;
            scope.destinations[ExitPoint::Regular] = check.openingBlock;
            scope.destinations[ExitPoint::Continue] = check.openingBlock;
            scope.commit();
        }
    } else if (condition->is(Kind::ForIn)) {
        throw;
    }
}
