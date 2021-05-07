#include <builder/builder.h>

#include <builder/manager.h>

#include <parser/type.h>
#include <parser/function.h>

#include <llvm/Support/Host.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>

namespace fs = std::filesystem;

BuilderResult::BuilderResult(Kind kind, Value *value, Typename type, std::unique_ptr<BuilderResult> implicit)
    : kind(kind), value(value), type(std::move(type)), implicit(std::move(implicit)) { }

BuilderType *Builder::makeType(const TypeNode *node) {
    auto iterator = types.find(node);

    if (iterator == types.end()) {
        auto ptr = std::make_unique<BuilderType>(node, *this);

        BuilderType *result = ptr.get();
        types[node] = std::move(ptr);

        result->build(); // needed to avoid recursive problems

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

        result->build();

        return result;
    } else {
        return iterator->second.get();
    }
}

Builder::Builder(const ManagerFile &file, const Options &opts)
    : root(file.root.get()), file(file), dependencies(file.resolve()),
    context(*file.manager.context), options(opts) {

    module = std::make_unique<Module>(file.path.filename().string(), context);

    module->setDataLayout(*file.manager.target.layout);
    module->setTargetTriple(file.manager.target.triple);

    for (const auto &node : root->children) {
        switch (node->is<Kind>()) {
            case Kind::Import:
                // Handled by ManagerFile
                break;

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
}
