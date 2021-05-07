#include <builder/builder.h>

#include <parser/for.h>
#include <parser/scope.h>

void BuilderScope::makeFor(const ForNode *node) {
    Node *expression = node->children.size() > 1 ? node->children.front().get() : nullptr;
    auto *code = node->children.back()->as<CodeNode>();

    if (!expression) {
        BuilderScope scope(code, *this);

        scope.current->CreateBr(scope.openingBlock);

        current->CreateBr(scope.openingBlock);

        currentBlock = BasicBlock::Create(
            function.builder.context, "", function.function, function.exitBlock);
        current->SetInsertPoint(currentBlock);
    } else if (expression->is(Kind::Expression)) {
        assert(false);
    } else if (expression->is(Kind::ForIn)) {
        assert(false);
    }
}
