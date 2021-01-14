#include <builder/builder.h>

#include <parser/block.h>
#include <parser/scope.h>

void BuilderScope::makeBlock(const BlockNode *node) {
    BuilderScope sub(node->children.front()->as<CodeNode>(), *this);

    currentBlock = BasicBlock::Create(function.builder.context, "", function.function, function.exitBlock);

    current.CreateBr(sub.openingBlock);
    current.SetInsertPoint(currentBlock);

    sub.current.CreateBr(currentBlock);

    mergeLifetimes(sub);
}
