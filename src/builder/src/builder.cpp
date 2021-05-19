#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/type.h>
#include <parser/function.h>
#include <parser/variable.h>

#include <llvm/Support/Host.h>

const Node *BuilderResult::first(::Kind nodeKind) {
    auto iterator = std::find_if(references.begin(), references.end(), [nodeKind](const Node *node) {
        return node->is(nodeKind);
    });

    return iterator == references.end() ? nullptr : *iterator;
}

BuilderResult::BuilderResult(Kind kind, Value *value, Typename type,
    std::unique_ptr<BuilderResult> implicit)
    : kind(kind), value(value), type(std::move(type)), implicit(std::move(implicit)) { }

BuilderResult::BuilderResult(const Node *from, std::vector<const Node *> references,
    std::unique_ptr<BuilderResult> implicit)
    : kind(BuilderResult::Kind::Unresolved), value(nullptr), from(from), references(std::move(references)),
    type(PrimitiveTypename { PrimitiveType::Unresolved }), implicit(std::move(implicit)) { }

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

BuilderVariable *Builder::makeGlobal(const VariableNode *node) {
    auto iterator = globals.find(node);

    if (iterator == globals.end()) {
        auto ptr = std::make_unique<BuilderVariable>(node, *this);

        BuilderVariable *result = ptr.get();
        globals[node] = std::move(ptr);

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

            case Kind::Variable:
                makeGlobal(node->as<VariableNode>());
                break;

            case Kind::Type: {
                auto e = node->as<TypeNode>();

                if (!e->isAlias)
                    makeType(e);
                break;
            }

            case Kind::Function:
                makeFunction(node->as<FunctionNode>());
                break;

            default:
                throw VerifyError(node.get(), "Cannot build this node in root.");
        }
    }
}
