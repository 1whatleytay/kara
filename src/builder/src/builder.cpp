#include <builder/builder.h>

#include <builder/error.h>

#include <parser/function.h>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>

namespace fs = std::filesystem;

Value *Result::get(IRBuilder<> &builder) const {
    return kind == Kind::Reference ? builder.CreateLoad(value) : value;
}

Builder::Builder(RootNode *root, const Options &options)
    : context(), module(fs::path(options.inputFile).filename().string(), context) {
    for (const auto &node : root->children) {
        switch (node->is<Kind>()) {
            case Kind::Function:
                makeFunction(node->as<FunctionNode>());
                break;
            default:
                throw VerifyError(node.get(), "Unknown root node kind {}.", node->kind);
        }
    }

    module.print(llvm::outs(), nullptr);
    verifyModule(module, &llvm::errs(), nullptr);
}
