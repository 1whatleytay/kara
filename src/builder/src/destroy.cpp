#include <builder/builder.h>

namespace kara::builder {
    uint64_t StatementContext::getNextUID() { return nextUID++; }

    void StatementContext::consider(const Result &result) {
        auto typeRef = std::get_if<utils::ReferenceTypename>(&result.type);

        if (parent.current
        && (result.isSet(builder::Result::FlagTemporary))
        && std::holds_alternative<utils::PrimitiveTypename>(result.type)
        && (!typeRef || typeRef->kind != utils::ReferenceKind::Regular)) {
            assert(!lock);

            toDestroy.push(result);
        }
    }

    void StatementContext::commit(llvm::BasicBlock *block) {
        if (!parent.current)
            return;

        llvm::IRBuilder<> builder(block);

        lock = true;

        while (!toDestroy.empty()) {
            const builder::Result &destroy = toDestroy.front();

            if (avoidDestroy.find(destroy.statementUID) != avoidDestroy.end())
                continue;

            parent.invokeDestroy(destroy, builder);

            toDestroy.pop();
        }

        lock = false;

        avoidDestroy.clear();
    }

    StatementContext::StatementContext(builder::Scope &parent) : parent(parent) { }
}