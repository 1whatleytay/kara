#include <builder/builder.h>

#include <builder/error.h>
#include <builder/target.h>
#include <builder/manager.h>
#include <builder/platform.h>

#include <cstdlib>
#include <parser/expression.h>
#include <parser/function.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <llvm/Support/Host.h>

#include <cassert>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::builder {
    builder::Cache *Cache::create() {
        auto ptr = std::make_unique<builder::Cache>(Cache { this });
        auto result = ptr.get();

        children.push_back(std::move(ptr));

        return result;
    }

    builder::Type *Builder::makeType(const parser::Type *node) {
        auto iterator = types.find(node);

        if (iterator == types.end()) {
            auto ptr = std::make_unique<builder::Type>(node, *this);

            builder::Type *result = ptr.get();
            types[node] = std::move(ptr);
            result->build(); // needed to avoid recursive problems

            return result;
        } else {
            return iterator->second.get();
        }
    }

    builder::Variable *Builder::makeGlobal(const parser::Variable *node) {
        auto iterator = globals.find(node);

        if (iterator == globals.end()) {
            auto ptr = std::make_unique<builder::Variable>(node, *this);

            builder::Variable *result = ptr.get();
            globals[node] = std::move(ptr);

            return result;
        } else {
            return iterator->second.get();
        }
    }

    builder::Function *Builder::makeFunction(const parser::Function *node) {
        auto iterator = functions.find(node);

        if (iterator == functions.end()) {
            auto ptr = std::make_unique<builder::Function>(node, *this);

            builder::Function *result = ptr.get();
            functions[node] = std::move(ptr);

            result->build();

            return result;
        } else {
            return iterator->second.get();
        }
    }

    llvm::Function *Builder::getMalloc() {
        auto existing = module->getFunction("malloc");

        if (existing)
            return existing;

        if (!mallocCache) {
            auto type = llvm::FunctionType::get(
                llvm::Type::getInt8PtrTy(context), { llvm::Type::getInt64Ty(context) }, false);

            mallocCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.malloc, *module);
        }

        return mallocCache;
    }

    llvm::Function *Builder::getFree() {
        auto existing = module->getFunction("free");

        if (existing)
            return existing;

        if (!freeCache) {
            auto type
                = llvm::FunctionType::get(llvm::Type::getVoidTy(context), { llvm::Type::getInt8PtrTy(context) }, false);

            freeCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.free, *module);
        }

        return freeCache;
    }

    llvm::Function *Builder::getRealloc() {
        // needs naming handled by builder
        auto existing = module->getFunction("realloc");

        if (existing)
            return existing;

        if (!reallocCache) {
            auto type = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(context),
                { llvm::Type::getInt8PtrTy(context), llvm::Type::getInt64Ty(context) }, false);

            reallocCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.realloc, *module);
        }

        return reallocCache;
    }

    llvm::StructType *Builder::makeOptionalType(const utils::Typename &type) {
        llvm::Type *subtype = makeTypename(type);

        auto holdsType = llvm::Type::getInt1Ty(context);

        /*
         * struct OptionalType {
         *   bool holds;
         *   int value;
         * }
         */

        return llvm::StructType::get(context, { holdsType, subtype });
    }

    llvm::StructType *Builder::makeVariableArrayType(const utils::Typename &type) {
        llvm::Type *subtype = makeTypename(type);

        auto sizeType = llvm::Type::getInt64Ty(context);
        auto pointerType = llvm::PointerType::get(subtype, 0);

        /*
         * struct VariableArrayOfInts {
         *   size_t size;
         *   size_t capacity;
         *   int *data;
         * };
         */

        return llvm::StructType::get(context, { sizeType, sizeType, pointerType });
    }

    const hermes::Node *Builder::lookupDestroy(const utils::Typename &type) {
        std::string name;

        if (auto other = std::get_if<utils::NamedTypename>(&type)) { // i think this is dangerous to support
            name = other->type->name;
        } else if (auto ref = std::get_if<utils::ReferenceTypename>(&type)) { // ? hmm this doesnt make sense idt
            if (auto sub = std::get_if<utils::NamedTypename>(ref->value.get())) {
                name = sub->type->name;
            }
        }

        if (name.empty())
            return nullptr;

        auto it = destroyInvocables.find(name);
        if (it == destroyInvocables.end())
            return nullptr;

        return it->second;
    }

    bool Builder::needsDestroy(const utils::Typename &type) {
        struct {
            Builder &builder;

            bool operator()(const utils::NamedTypename &type) {
                auto base = builder.makeType(type.type); // cycle?
                return builder.lookupDestroy(type) || base->implicitDestructor; // might need some better checking here
            }
            bool operator()(const utils::ArrayTypename &type) { return type.kind == utils::ArrayKind::VariableSize; }
            bool operator()(const utils::FunctionTypename &type) { return type.kind == utils::FunctionKind::Regular; }
            bool operator()(const utils::OptionalTypename &type) {
                return builder.needsDestroy(*type.value); // ?
            }
            bool operator()(const utils::PrimitiveTypename &type) { return false; }
            bool operator()(const utils::ReferenceTypename &type) { return type.kind != utils::ReferenceKind::Regular; }
        } visitor { *this };

        return std::visit(visitor, type);
    }

    Builder::Builder(const SourceFile &file, SourceManager &manager, const Target &target, const options::Options &opts)
        : root(file.root.get())
        , file(file)
        , manager(manager)
        , target(target)
        , dependencies(manager.resolve(file))
        , context(*target.context)
        , options(opts) {

        if (opts.rawPlatform)
            platform = std::make_unique<Platform>();
        else
            platform = Platform::byTriple(target.triple);

        module = std::make_unique<llvm::Module>(fs::path(file.path).filename().string(), context);

        module->setDataLayout(*target.layout);
        module->setTargetTriple(target.triple);

        auto destroyInvocablesRaw = searchAllDependencies([](const hermes::Node *node) -> bool {
            if (!node->is(parser::Kind::Function))
                return false;

            auto e = node->as<parser::Function>();

            return e->name == "destroy" && e->parameterCount == 1;
        });

        destroyInvocables.reserve(destroyInvocablesRaw.size());
        for (auto invocable : destroyInvocablesRaw) {
            auto function = invocable->as<parser::Function>();

            auto parameters = function->parameters();
            if (parameters.size() != 1)
                continue;

            auto parameter = parameters.front();
            assert(parameter->hasFixedType);

            auto nodeType = parameter->fixedType();
            auto type = resolveTypename(nodeType);

            std::string name;
            if (auto other = std::get_if<utils::NamedTypename>(&type)) { // i think this is dangerous to support
                name = other->type->name;
            } else if (auto ref = std::get_if<utils::ReferenceTypename>(&type)) {
                if (auto sub = std::get_if<utils::NamedTypename>(ref->value.get())) {
                    name = sub->type->name;
                }
            }

            if (name.empty())
                continue;

            destroyInvocables[name] = function;
        }

        for (const auto &node : root->children) {
            switch (node->is<parser::Kind>()) {
            case parser::Kind::Import:
                // Handled by SourceFile
                break;

            case parser::Kind::Variable:
                makeGlobal(node->as<parser::Variable>());
                break;

            case parser::Kind::Type: {
                auto e = node->as<parser::Type>();

                if (!e->isAlias)
                    makeType(e);
                break;
            }

            case parser::Kind::Function:
                makeFunction(node->as<parser::Function>());
                break;

            default:
                throw VerifyError(node.get(), "Cannot build this node in root.");
            }
        }
    }
}
