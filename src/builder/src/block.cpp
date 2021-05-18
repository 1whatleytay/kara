#include <builder/builder.h>

#include <builder/error.h>

#include <parser/scope.h>

void BuilderScope::makeBlock(const BlockNode *node) {
    assert(current);

    BuilderScope sub(node->children.front()->as<CodeNode>(), *this);

    currentBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);

    current->CreateBr(sub.openingBlock);
    current->SetInsertPoint(currentBlock);

    if (!sub.currentBlock->getTerminator())
        sub.current->CreateBr(currentBlock);
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

                    current->CreateBr(terminator.openingBlock);

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
        if (scope->current) {
            scope->current->CreateBr(currentBlock);
        }
    }
}

void BuilderScope::makeFor(const ForNode *node) {
    const Node *condition = node->condition();
    auto *code = node->body();

    if (!condition) {
        BuilderScope scope(code, *this);

        if (current) {
            currentBlock = BasicBlock::Create(
                function.builder.context, "", function.function, function.exitBlock);

            current->CreateBr(scope.openingBlock);
            scope.current->CreateBr(scope.openingBlock);

            current->SetInsertPoint(currentBlock);
        }
    } else if (condition->is(Kind::Expression)) {
        BuilderScope check(condition, *this);

        assert(check.product && check.product->type == PrimitiveTypename::from(PrimitiveType::Bool));

        BuilderScope scope(code, *this);

        if (current) {
            currentBlock = BasicBlock::Create(
                function.builder.context, "", function.function, function.exitBlock);

            current->CreateBr(check.openingBlock);
            check.current->CreateCondBr(check.get(*check.product), scope.openingBlock, currentBlock);
            scope.current->CreateBr(check.openingBlock);

            current->SetInsertPoint(currentBlock);
        }
    } else if (condition->is(Kind::ForIn)) {
        assert(false);
    }
}
