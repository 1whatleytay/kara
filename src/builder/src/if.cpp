#include <builder/builder.h>

#include <builder/error.h>

#include <parser/if.h>
#include <parser/scope.h>

void BuilderScope::makeIf(const IfNode *node) {
    std::vector<BuilderScope> branches;

    while (node) {
        BuilderScope sub(node->children[1]->as<CodeNode>(), *this);

        currentBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);

        BuilderResult conditionResult = makeExpression(node->children.front()->as<ExpressionNode>()->result);
        Value *condition = get(conditionResult);

        if (conditionResult.type != TypenameNode::boolean)
            throw VerifyError(node->children.front().get(), "Condition for if statement must be a bool.");

        current.CreateCondBr(condition, sub.openingBlock, currentBlock);
        current.SetInsertPoint(currentBlock);

        branches.push_back(std::move(sub));

        if (node->children.size() == 3) {
            // has else branch
            switch (node->children[2]->is<Kind>()) {
                case Kind::If:
                    node = node->children[2]->as<IfNode>();
                    break;

                case Kind::Code: {
                    BuilderScope terminator(node->children[2]->as<CodeNode>(), *this);

                    current.CreateBr(terminator.openingBlock);

                    currentBlock = BasicBlock::Create(
                        function.builder.context, "", function.function, function.exitBlock);
                    current.SetInsertPoint(currentBlock);

                    branches.push_back(std::move(terminator));

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

    for (auto &branch : branches) {
        if (!branch.currentBlock->getTerminator()) {
            mergePossibleLifetimes(branch);

            branch.current.CreateBr(currentBlock);
        }
    }
}
