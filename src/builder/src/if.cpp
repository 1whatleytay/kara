#include <builder/builder.h>

#include <builder/error.h>

#include <parser/if.h>
#include <parser/scope.h>

void BuilderScope::makeIf(const IfNode *node) {
    assert(current);

    while (node) {
        BuilderScope sub(node->children[1]->as<CodeNode>(), *this);

        currentBlock = BasicBlock::Create(*function.builder.context, "", function.function, function.exitBlock);

        BuilderResult conditionResult = makeExpression(node->children.front()->as<ExpressionNode>());
        std::optional<BuilderResult> conditionConverted = convert(conditionResult, types::boolean());

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
                    BuilderScope terminator(node->children[2]->as<CodeNode>(), *this);

                    current->CreateBr(terminator.openingBlock);

                    currentBlock = BasicBlock::Create(
                        *function.builder.context, "", function.function, function.exitBlock);
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
}
