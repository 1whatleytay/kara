#include <builder/builder.h>

#include <builder/operations.h>

#include <cassert>

namespace kara::builder {
    uint64_t Accumulator::getNextUID() { return nextUID++; }

    void Accumulator::consider(const Result &result) {
        auto typeRef = std::get_if<utils::ReferenceTypename>(&result.type);

        auto isTemporary = result.isSet(builder::Result::FlagTemporary);

        // oh my god, I don't need these checks anymore because accumulator is optional in ops::Context wow
        auto isPrimitive = std::holds_alternative<utils::PrimitiveTypename>(result.type);
        auto isRegularReference = typeRef && typeRef->kind == utils::ReferenceKind::Regular;

        if (isTemporary && !isPrimitive && !isRegularReference) {
            assert(!lock);

            toDestroy.push(result);
        }
    }

    void Accumulator::commit(const ops::Context &context) {
        lock = true;

        while (!toDestroy.empty()) {
            const builder::Result &destroy = toDestroy.front();

            if (avoidDestroy.find(destroy.uid) == avoidDestroy.end())
                ops::makeDestroy(context, ops::ref(context, destroy), destroy.type);

            toDestroy.pop();
        }

        lock = false;

        avoidDestroy.clear();
    }
}