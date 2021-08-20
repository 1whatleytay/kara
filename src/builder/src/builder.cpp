#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/type.h>
#include <parser/function.h>
#include <parser/variable.h>

#include <llvm/Support/Host.h>

uint64_t BuilderStatementContext::getNextUID() { return nextUID++; }

void BuilderStatementContext::consider(const BuilderResult &result) {
    auto typeRef = std::get_if<ReferenceTypename>(&result.type);

    if (parent.current
        && (result.isSet(BuilderResult::FlagTemporary))
        && std::holds_alternative<PrimitiveTypename>(result.type)
        && (!typeRef || typeRef->kind != ReferenceKind::Regular)) {
        assert(!lock);

        toDestroy.push(result);
    }
}

void BuilderStatementContext::commit(BasicBlock *block) {
//    if (instructions && block) {
//        // no more, need BR refactor
//        auto &instIn = instructions->getInstList();
//
//        while (!instIn.empty()) {
//            instIn.front().moveBefore(*block, block->end());
//        }
//
//        instIn.clear();
//    }

    if (!parent.current)
        return;

    IRBuilder<> builder(block);

    lock = true;

    while (!toDestroy.empty()) {
        const BuilderResult &destroy = toDestroy.front();

        if (avoidDestroy.find(destroy.statementUID) != avoidDestroy.end())
            continue;

        parent.invokeDestroy(destroy, builder);

        toDestroy.pop();
    }

    lock = false;

    avoidDestroy.clear();
}

BuilderStatementContext::BuilderStatementContext(BuilderScope &parent) : parent(parent) { }

bool BuilderResult::isSet(Flags flag) const {
    return flags & flag;
}

const Node *BuilderResult::first(::Kind nodeKind) {
    auto iterator = std::find_if(references.begin(), references.end(), [nodeKind](const Node *node) {
        return node->is(nodeKind);
    });

    return iterator == references.end() ? nullptr : *iterator;
}

// oh dear
BuilderResult::BuilderResult(uint32_t flags, Value *value, Typename type,
    BuilderStatementContext *statementContext, std::unique_ptr<BuilderResult> implicit)
    : flags(flags), value(value), type(std::move(type)), implicit(std::move(implicit)) {

    if (statementContext) {
        statementUID = statementContext->getNextUID();
        statementContext->consider(*this); // register
    }
}

BuilderResult::BuilderResult(const Node *from, std::vector<const Node *> references,
    BuilderStatementContext *statementContext, std::unique_ptr<BuilderResult> implicit)
    : flags(FlagUnresolved), value(nullptr), from(from), references(std::move(references)),
    type(PrimitiveTypename { PrimitiveType::Unresolved }), implicit(std::move(implicit)) {

    if (statementContext) {
        statementUID = statementContext->getNextUID();
        statementContext->consider(*this); // register
    }
}

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

Function *Builder::getMalloc() {
    if (!mallocCache) {
        auto type = FunctionType::get(
            Type::getInt8PtrTy(context),
            std::vector<Type *> { Type::getInt64Ty(context) },
            false);

        mallocCache = Function::Create(type, GlobalVariable::LinkageTypes::ExternalLinkage, options.malloc, *module);
    }

    return mallocCache;
}

Function *Builder::getFree() {
    if (!freeCache) {
        auto type = FunctionType::get(
            Type::getVoidTy(context),
            std::vector<Type *> { Type::getInt8PtrTy(context) },
            false);

        freeCache = Function::Create(type, GlobalVariable::LinkageTypes::ExternalLinkage, options.free, *module);
    }

    return freeCache;
}

Value *BuilderScope::makeAlloca(const Typename &type, const std::string &name) {
    assert(function);

    if (auto array = std::get_if<ArrayTypename>(&type)) {
        if (array->kind == ArrayKind::Unbounded)
            throw std::runtime_error(
                fmt::format("Attempt to allocate type {} on stack.", toString(type)));

        if (array->kind == ArrayKind::UnboundedSized)
            throw std::runtime_error(
                fmt::format("VLA unsupported for type {0}. Use *{0} for allocation instead.", toString(type)));
    }

    return function->entry.CreateAlloca(builder.makeTypename(type), nullptr, name);
}

Value *BuilderScope::makeMalloc(const Typename &type, const std::string &name) {
    Value *arraySize = nullptr;

    if (auto array = std::get_if<ArrayTypename>(&type)) {
        if (array->kind == ArrayKind::Unbounded)
            throw std::runtime_error(
                fmt::format("Attempt to allocate type {} on heap.", toString(type)));

        if (array->kind == ArrayKind::UnboundedSized) {
            assert(array->expression);

            auto it = expressionCache.find(array->expression);
            if (it != expressionCache.end()) {
                arraySize = get(it->second);
            } else {
                auto converted = convert(makeExpression(array->expression), PrimitiveTypename { PrimitiveType::ULong });

                if (!converted)
                    throw VerifyError(array->expression, "Expression cannot be converted to ulong for size for array.");

                auto &result = *converted;

                expressionCache.insert({ array->expression, result });

                arraySize = get(result);
            }

            // TODO: needs recursive implementation of sizes to account for [[int:50]:50]
            // ^ probably would be done in the great refactor


            auto llvmElementType = builder.makeTypename(*array->value);
            size_t elementSize = builder.file.manager.target.layout->getTypeStoreSize(llvmElementType);

            Constant *llvmElementSize = ConstantInt::get(Type::getInt64Ty(builder.context), elementSize);

            if (current)
                arraySize = current->CreateMul(llvmElementSize, arraySize);
        }
    }

    if (!current)
        return nullptr;

    auto llvmType = builder.makeTypename(type);
    auto pointerType = PointerType::get(llvmType, 0);

    size_t bytes = builder.file.manager.target.layout->getTypeStoreSize(llvmType);
    auto malloc = builder.getMalloc();

    // if statement above can adjust size...
    if (!arraySize) {
        arraySize = ConstantInt::get(Type::getInt64Ty(builder.context), bytes);
    }

    return current->CreatePointerCast(current->CreateCall(malloc, { arraySize }), pointerType, name);
}

Builder::Builder(const ManagerFile &file, const Options &opts)
    : root(file.root.get()), file(file), dependencies(file.resolve()),
    context(*file.manager.context), options(opts) {

    module = std::make_unique<Module>(file.path.filename().string(), context);

    module->setDataLayout(*file.manager.target.layout);
    module->setTargetTriple(file.manager.target.triple);

    destroyInvokables = searchAllDependencies([](const Node *node) -> bool {
        if (!node->is(Kind::Function))
            return false;

        auto e = node->as<FunctionNode>();

        return e->name == "destroy" && e->parameterCount == 1;
    });

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
