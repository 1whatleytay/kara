#include <builder/builder.h>

#include <builder/error.h>

#include <parser/function.h>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>

namespace fs = std::filesystem;

BuilderResult::BuilderResult(Kind kind, Value *value, Typename type)
    : kind(kind), value(value), type(std::move(type)) { }

Builder::Builder(RootNode *root, Options passedOptions)
    : context(), module(fs::path(options.inputFile).filename().string(), context), options(std::move(passedOptions)) {
    for (const auto &node : root->children) {
        switch (node->is<Kind>()) {
            case Kind::Function:
                functions[node->as<FunctionNode>()] =
                    std::make_unique<BuilderFunction>(node->as<FunctionNode>(), *this);
                break;
            default:
                throw VerifyError(node.get(), "Unknown root node kind {}.", node->kind);
        }
    }

    if (options.printIR)
        module.print(llvm::outs(), nullptr);

    verifyModule(module, &llvm::errs(), nullptr);
}
