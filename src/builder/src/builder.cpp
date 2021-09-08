#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/expression.h>
#include <parser/function.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <llvm/Support/Host.h>

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
        if (!mallocCache) {
            auto type = llvm::FunctionType::get(
                llvm::Type::getInt8PtrTy(context), { llvm::Type::getInt64Ty(context) }, false);

            mallocCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.malloc, *module);
        }

        return mallocCache;
    }

    llvm::Function *Builder::getFree() {
        if (!freeCache) {
            auto type
                = llvm::FunctionType::get(llvm::Type::getVoidTy(context), { llvm::Type::getInt8PtrTy(context) }, false);

            freeCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.free, *module);
        }

        return freeCache;
    }

    llvm::Function *Builder::getRealloc() {
        if (!reallocCache) {
            auto type = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(context),
                { llvm::Type::getInt8PtrTy(context), llvm::Type::getInt64Ty(context) }, false);

            reallocCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.realloc, *module);
        }

        return reallocCache;
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

    Builder::Builder(const ManagerFile &file, const options::Options &opts)
        : root(file.root.get())
        , file(file)
        , dependencies(file.resolve())
        , context(*file.manager.context)
        , options(opts) {

        module = std::make_unique<llvm::Module>(file.path.filename().string(), context);

        module->setDataLayout(*file.manager.target.layout);
        module->setTargetTriple(file.manager.target.triple);

        destroyInvocables = searchAllDependencies([](const hermes::Node *node) -> bool {
            if (!node->is(parser::Kind::Function))
                return false;

            auto e = node->as<parser::Function>();

            return e->name == "destroy" && e->parameterCount == 1;
        });

        for (const auto &node : root->children) {
            switch (node->is<parser::Kind>()) {
            case parser::Kind::Import:
                // Handled by ManagerFile
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
