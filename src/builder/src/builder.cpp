#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/expression.h>
#include <parser/function.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <llvm/Support/Host.h>

namespace kara::builder {
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
            auto type = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(context),
                std::vector<llvm::Type *> { llvm::Type::getInt64Ty(context) }, false);

            mallocCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.malloc, *module);
        }

        return mallocCache;
    }

    llvm::Function *Builder::getFree() {
        if (!freeCache) {
            auto type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(context), std::vector<llvm::Type *> { llvm::Type::getInt8PtrTy(context) }, false);

            freeCache = llvm::Function::Create(
                type, llvm::GlobalVariable::LinkageTypes::ExternalLinkage, options.free, *module);
        }

        return freeCache;
    }

    llvm::Value *Scope::makeAlloca(const utils::Typename &type, const std::string &name) {
        assert(function);

        if (auto array = std::get_if<utils::ArrayTypename>(&type)) {
            if (array->kind == utils::ArrayKind::Unbounded)
                throw std::runtime_error(fmt::format("Attempt to allocate type {} on stack.", toString(type)));

            if (array->kind == utils::ArrayKind::UnboundedSized)
                throw std::runtime_error(
                    fmt::format("VLA unsupported for type {0}. Use *{0} for allocation instead.", toString(type)));
        }

        return function->entry.CreateAlloca(builder.makeTypename(type), nullptr, name);
    }

    llvm::Value *Scope::makeMalloc(const utils::Typename &type, const std::string &name) {
        llvm::Value *arraySize = nullptr;

        if (auto array = std::get_if<utils::ArrayTypename>(&type)) {
            if (array->kind == utils::ArrayKind::Unbounded)
                throw std::runtime_error(fmt::format("Attempt to allocate type {} on heap.", toString(type)));

            if (array->kind == utils::ArrayKind::UnboundedSized) {
                assert(array->expression);

                auto it = expressionCache.find(array->expression);
                if (it != expressionCache.end()) {
                    arraySize = get(it->second);
                } else {
                    auto converted = convert(
                        makeExpression(array->expression), utils::PrimitiveTypename { utils::PrimitiveType::ULong });

                    if (!converted)
                        throw VerifyError(
                            array->expression, "Expression cannot be converted to ulong for size for array.");

                    auto &result = *converted;

                    expressionCache.insert({ array->expression, result });

                    arraySize = get(result);
                }

                // TODO: needs recursive implementation of sizes to account for
                // [[int:50]:50] ^ probably would be done in the great refactor

                auto llvmElementType = builder.makeTypename(*array->value);
                size_t elementSize = builder.file.manager.target.layout->getTypeStoreSize(llvmElementType);

                llvm::Constant *llvmElementSize
                    = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.context), elementSize);

                if (current)
                    arraySize = current->CreateMul(llvmElementSize, arraySize);
            }
        }

        if (!current)
            return nullptr;

        auto llvmType = builder.makeTypename(type);
        auto pointerType = llvm::PointerType::get(llvmType, 0);

        size_t bytes = builder.file.manager.target.layout->getTypeStoreSize(llvmType);
        auto malloc = builder.getMalloc();

        // if statement above can adjust size...
        if (!arraySize) {
            arraySize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.context), bytes);
        }

        return current->CreatePointerCast(current->CreateCall(malloc, { arraySize }), pointerType, name);
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

        destroyInvokables = searchAllDependencies([](const hermes::Node *node) -> bool {
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
