#include <builder/builder.h>

#include <hermes/node.h>

namespace kara::builder {
    bool Result::isSet(Flags flag) const { return flags & flag; }

    // oh dear
    Result::Result(uint32_t flags, llvm::Value *value, utils::Typename type, Accumulator *accumulator)
        : flags(flags)
        , value(value)
        , type(std::move(type)) {

        if (accumulator) {
            uid = accumulator->getNextUID();
            accumulator->consider(*this); // register
        }
    }

    Unresolved::Unresolved(
        const hermes::Node *from, std::vector<const hermes::Node *> references, std::unique_ptr<Result> implicit)
        : from(from)
        , references(std::move(references))
        , implicit(std::move(implicit)) { }
}