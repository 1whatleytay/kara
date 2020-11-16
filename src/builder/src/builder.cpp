#include <builder/builder.h>

#include <builder/error.h>

#include <parser/function.h>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>

namespace fs = std::filesystem;

BuilderResult::BuilderResult(Kind kind, Value *value, Typename type,
    int32_t lifetimeDepth, std::shared_ptr<MultipleLifetime> lifetime)
    : kind(kind), value(value), type(std::move(type)), lifetimeDepth(lifetimeDepth), lifetime(std::move(lifetime)) { }

Builder::Builder(RootNode *root, const Options &options)
    : context(), module(fs::path(options.inputFile).filename().string(), context) {
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
