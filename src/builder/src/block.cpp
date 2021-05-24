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

    BuilderScope sub(node->children.front()->as<CodeNode>(), *this);

    current->CreateBr(sub.openingBlock);

    currentBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);
    current->SetInsertPoint(currentBlock);

    br(sub.currentBlock, currentBlock);
}

void BuilderScope::makeIf(const IfNode *node) {
    assert(current);

    std::vector<std::unique_ptr<BuilderScope>> scopes;

    while (node) {
        scopes.push_back(std::make_unique<BuilderScope>(node->children[1]->as<CodeNode>(), *this));
        BuilderScope &sub = *scopes.back();

        currentBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);

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
                        function.builder.context, "", function.function, function.exitBlock);
                    current->SetInsertPoint(currentBlock);

                    node = nullptr;

                    break;
                }

                default:
                    assert(false);
            }
        } else {
            // no branch, all is good
            node = nullptr;
        }
    }

    for (const auto &scope : scopes) {
        if (scope->current)
            br(scope->currentBlock, currentBlock);
    }
}

void BuilderScope::makeFor(const ForNode *node) {
    const Node *condition = node->condition();
    auto *code = node->body();

    if (!condition) {
        assert(current);

        if (current) { // cleanup is possible using method described in if std::vector<...> scopes
            BasicBlock *entry = BasicBlock::Create(
                function.builder.context, "loop_entry", function.function, function.exitBlock);

            BasicBlock *exit = BasicBlock::Create(
                function.builder.context, "loop_exit", function.function, function.exitBlock);

            BuilderScope scope(code, *this, true, entry, exit);

            IRBuilder<>(entry).CreateBr(scope.openingBlock);

            current->CreateBr(entry);
            br(scope.currentBlock, entry);

            currentBlock = exit;
            current->SetInsertPoint(currentBlock);
        }
    } else if (condition->is(Kind::Expression)) {
        assert(current);

        if (current) {
            BasicBlock *entry = BasicBlock::Create(
                function.builder.context, "loop_entry", function.function, function.exitBlock);

            BasicBlock *exit = BasicBlock::Create(
                function.builder.context, "loop_exit", function.function, function.exitBlock);

            BuilderScope check(condition, *this, true, entry, exit);

            assert(check.product);
            auto converted = convert(*check.product, PrimitiveTypename { PrimitiveType::Bool });

            if (!converted)
                throw VerifyError(node, "For node must have bool as expression, got {}.", toString(check.product->type));

            BuilderScope scope(code, *this, entry, exit);

            currentBlock = exit;

            IRBuilder<>(entry).CreateBr(check.openingBlock);

            current->CreateBr(entry);
            check.current->CreateCondBr(check.get(*check.product), scope.openingBlock, exit);
            br(scope.currentBlock, entry);

            current->SetInsertPoint(currentBlock);
        }
    } else if (condition->is(Kind::ForIn)) {
        assert(false);
    }
}
