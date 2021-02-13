#include <builder/builder.h>

#include <parser/type.h>
#include <parser/function.h>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>

namespace fs = std::filesystem;

BuilderResult::BuilderResult(Kind kind, Value *value, Typename type)
    : kind(kind), value(value), type(std::move(type)) { }

BuilderType *Builder::makeType(const TypeNode *node) {
    auto iterator = types.find(node);

    if (iterator == types.end()) {
        auto ptr = std::make_unique<BuilderType>(node, *this);

        BuilderType *result = ptr.get();
        types[node] = std::move(ptr);

        return result;
    } else {
        return iterator->second.get();
    }
}

BuilderFunction *Builder::makeFunction(const FunctionNode *node) {
    auto iterator = functions.find(node);

    if (iterator == functions.end()) {
        auto ptr = std::make_unique<BuilderFunction>(node, *this);

        BuilderFunction *result = ptr.get();
        functions[node] = std::move(ptr);

        return result;
    } else {
        return iterator->second.get();
    }
}

Builder::Builder(RootNode *root, Options passedOptions)
    : context(), module(fs::path(options.inputFile).filename().string(), context), options(std::move(passedOptions)) {
    for (const auto &node : root->children) {
        switch (node->is<Kind>()) {
            case Kind::Type:
                makeType(node->as<TypeNode>());
                break;

            case Kind::Function:
                makeFunction(node->as<FunctionNode>());
                break;

            default:
                assert(false);
        }
    }

    if (options.printIR)
        module.print(llvm::outs(), nullptr);

    verifyModule(module, &llvm::errs(), nullptr);
}
