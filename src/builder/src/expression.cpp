#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/expression.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/operator.h>
#include <parser/type.h>
#include <parser/variable.h>

namespace kara::builder {
    Wrapped Scope::makeExpressionNounContent(const hermes::Node *node) {
        switch (node->is<parser::Kind>()) {
        case parser::Kind::Parentheses:
            return makeExpression(node->as<parser::Parentheses>()->body());

        case parser::Kind::Reference: {
            auto e = node->as<parser::Reference>();

            return Unresolved(e, builder.findAll(e));
        }

        case parser::Kind::Special: {
            assert(node->as<parser::Special>()->type == parser::Special::Type::Null);

            return builder::Result(builder::Result::FlagTemporary,
                llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(builder.context)),
                utils::PrimitiveTypename { utils::PrimitiveType::Null }, &statementContext);
        }

        case parser::Kind::Bool:
            return builder::Result(builder::Result::FlagTemporary,
                llvm::ConstantInt::get(llvm::Type::getInt1Ty(builder.context), node->as<parser::Bool>()->value),
                utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);

        case parser::Kind::String: {
            auto *e = node->as<parser::String>();

            assert(e->inserts.empty());

            llvm::Value *ptr = nullptr;

            if (current) {
                llvm::Constant *initial = llvm::ConstantDataArray::getString(builder.context, e->text);

                std::string convertedText(e->text.size(), '.');

                std::transform(e->text.begin(), e->text.end(), convertedText.begin(),
                    [](char c) { return (std::isalpha(c) || std::isdigit(c)) ? c : '_'; });

                auto variable = new llvm::GlobalVariable(*builder.module, initial->getType(), true,
                    llvm::GlobalVariable::LinkageTypes::PrivateLinkage, initial, fmt::format("str_{}", convertedText));

                ptr = current->CreateStructGEP(variable, 0);
            }

            return builder::Result(builder::Result::FlagTemporary, ptr,

                utils::ReferenceTypename {
                    std::make_shared<utils::Typename>(utils::ArrayTypename { utils::ArrayKind::Unbounded,

                        std::make_shared<utils::Typename>(utils::PrimitiveTypename { utils::PrimitiveType::Byte }) }),
                    false },
                &statementContext);
        }

        case parser::Kind::Array: {
            auto *e = node->as<parser::Array>();

            auto elements = e->elements();
            std::vector<builder::Result> results;
            results.reserve(elements.size());

            std::transform(elements.begin(), elements.end(), std::back_inserter(results),
                [this](auto x) { return makeExpression(x); });

            utils::Typename subType
                = results.empty() ? utils::PrimitiveTypename::from(utils::PrimitiveType::Any) : results.front().type;

            if (!std::all_of(results.begin(), results.end(),
                    [&subType](const builder::Result &result) { return result.type == subType; })) {
                throw VerifyError(e, "Array elements must all be the same type ({}).", toString(subType));
            }

            utils::ArrayTypename type
                = { utils::ArrayKind::FixedSize, std::make_shared<utils::Typename>(subType), results.size() };

            llvm::Type *arrayType = builder.makeTypename(type);

            llvm::Value *value = nullptr;

            if (current) {
                assert(function);

                value = function->entry.CreateAlloca(arrayType);

                for (size_t a = 0; a < results.size(); a++) {
                    const builder::Result &result = results[a];

                    llvm::Value *index = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.context), a);
                    llvm::Value *point = current->CreateInBoundsGEP(
                        value, { llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.context), 0), index });

                    current->CreateStore(get(result), point);
                }
            }

            return builder::Result(
                builder::Result::FlagTemporary | builder::Result::FlagReference, value, type, &statementContext);
        }

        case parser::Kind::Number: {
            auto *e = node->as<parser::Number>();

            struct {
                llvm::LLVMContext &context;
                builder::StatementContext *statementContext;

                builder::Result operator()(int64_t s) {
                    return builder::Result { builder::Result::FlagTemporary,
                        llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(context), s),
                        utils::PrimitiveTypename { utils::PrimitiveType::Long }, statementContext };
                }

                builder::Result operator()(uint64_t u) {
                    return builder::Result { builder::Result::FlagTemporary,
                        llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), u),
                        utils::PrimitiveTypename { utils::PrimitiveType::ULong }, statementContext };
                }

                builder::Result operator()(double f) {
                    return builder::Result { builder::Result::FlagTemporary,
                        llvm::ConstantFP::get(llvm::Type::getDoubleTy(context), f),
                        utils::PrimitiveTypename { utils::PrimitiveType::Double }, statementContext };
                }
            } visitor { builder.context, &statementContext };

            return std::visit(visitor, e->value);
        }

        case parser::Kind::New: {
            auto *e = node->as<parser::New>();

            return builder::Unresolved(e, { e });
        }

        default:
            throw;
        }
    }

    builder::Wrapped Scope::makeExpressionNounModifier(const hermes::Node *node, const builder::Wrapped &result) {
        switch (node->is<parser::Kind>()) {
        case parser::Kind::Call: {
            auto callNode = node->as<parser::Call>();

            auto callNames = callNode->names();
            auto callParameters = callNode->parameters();

            assert(std::holds_alternative<builder::Unresolved>(result));

            auto &unresolved = std::get<builder::Unresolved>(result);

            size_t extraParameter = unresolved.implicit ? 1 : 0;

            MatchInput callInput;
            callInput.parameters.resize(callParameters.size() + extraParameter);

            if (unresolved.implicit)
                callInput.parameters[0] = unresolved.implicit.get();

            std::vector<builder::Result> resultLifetimes;
            resultLifetimes.reserve(callParameters.size());

            for (auto c : callParameters)
                resultLifetimes.push_back(makeExpression(c));

            for (size_t a = 0; a < resultLifetimes.size(); a++)
                callInput.parameters[a + extraParameter] = &resultLifetimes[a];

            callInput.names = callNode->namesStripped();

            auto isNewNode = [](const hermes::Node *n) { return n->is(parser::Kind::New); };
            auto newIt = std::find_if(unresolved.references.begin(), unresolved.references.end(), isNewNode);

            if (newIt != unresolved.references.end()) {
                auto newNode = (*newIt)->as<parser::New>();
                auto type = builder.resolveTypename(newNode->type());

                auto typeNode = std::get_if<utils::NamedTypename>(&type);

                if (!typeNode)
                    throw VerifyError(newNode, "New parameters may only be passed to a type/struct.");

                auto value = callUnpack(call({ typeNode->type }, callInput), unresolved.from);

                auto output = makeNew(newNode);

                if (current)
                    current->CreateStore(get(value), get(output));

                return output;
            }

            std::vector<const hermes::Node *> functions;

            auto callable
                = [](const hermes::Node *n) { return n->is(parser::Kind::Function) || n->is(parser::Kind::Type); };

            std::copy_if(
                unresolved.references.begin(), unresolved.references.end(), std::back_inserter(functions), callable);

            if (functions.empty())
                throw VerifyError(node, "Reference did not resolve to any functions to call.");

            return callUnpack(call(functions, callInput), unresolved.from);
        }

        case parser::Kind::Dot: {
            builder::Result sub = infer(result);

            auto refNode = node->children.front()->as<parser::Reference>();

            // Set up to check if property exists, dereference if needed
            utils::Typename *subtype = &sub.type;

            size_t numReferences = 0;

            while (auto *type = std::get_if<utils::ReferenceTypename>(subtype)) {
                subtype = type->value.get();
                numReferences++;
            }

            if (auto *type = std::get_if<utils::NamedTypename>(subtype)) {
                builder::Type *builderType = builder.makeType(type->type);

                auto match = [refNode](auto var) { return var->name == refNode->name; };

                auto fields = type->type->fields();
                auto iterator = std::find_if(fields.begin(), fields.end(), match);

                if (iterator != fields.end()) {
                    auto *varNode = *iterator;

                    if (!varNode->hasFixedType)
                        throw VerifyError(varNode, "All struct variables must have fixed type.");

                    size_t index = builderType->indices.at(varNode);

                    // I feel uneasy touching this...
                    llvm::Value *structRef = numReferences > 0 ? get(sub) : ref(sub);

                    if (current) {
                        for (size_t a = 1; a < numReferences; a++)
                            structRef = current->CreateLoad(structRef);
                    }

                    return builder::Result((sub.flags & (builder::Result::FlagMutable | builder::Result::FlagTemporary))
                            | builder::Result::FlagReference,
                        current ? current->CreateStructGEP(structRef, index, refNode->name) : nullptr,
                        builder.resolveTypename(varNode->fixedType()), &statementContext);
                }
            }

            const auto &global = builder.root->children;

            auto matchFunction = [&](const hermes::Node *node) {
                if (!node->is(parser::Kind::Function))
                    return false;

                auto *e = node->as<parser::Function>();
                if (e->name != refNode->name || e->parameterCount == 0)
                    return false;

                return true;
            };

            auto n = builder.searchAllDependencies(matchFunction);

            if (n.empty())
                throw VerifyError(node, "Could not find method or field with name {}.", refNode->name);

            return builder::Unresolved(node, n, std::make_unique<builder::Result>(std::move(sub)));
        }

        case parser::Kind::Index: {
            builder::Result sub = unpack(infer(result));

            const utils::ArrayTypename *arrayType = std::get_if<utils::ArrayTypename>(&sub.type);

            if (!arrayType) {
                throw VerifyError(
                    node, "Indexing must only be applied on array types, type is {}.", toString(sub.type));
            }

            auto *indexExpression = node->children.front()->as<parser::Expression>();

            builder::Result index = makeExpression(indexExpression);
            std::optional<builder::Result> indexConverted
                = convert(index, utils::PrimitiveTypename { utils::PrimitiveType::ULong });

            if (!indexConverted.has_value()) {
                throw VerifyError(indexExpression,
                    "Must be able to be converted to int type for "
                    "indexing, instead type is {}.",
                    toString(index.type));
            }

            index = indexConverted.value();

            auto indexArray = [&]() -> llvm::Value * {
                if (!current)
                    return nullptr;

                switch (arrayType->kind) {
                case utils::ArrayKind::FixedSize:
                    return current->CreateGEP(
                        ref(sub), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.context), 0), get(index) });

                case utils::ArrayKind::Unbounded:
                case utils::ArrayKind::UnboundedSized: // TODO: no good for stack
                    // allocated arrays
                    return current->CreateGEP(ref(sub), get(index));

                default:
                    throw std::exception();
                }
            };

            return builder::Result((sub.flags & (builder::Result::FlagMutable | builder::Result::FlagTemporary))
                    | builder::Result::FlagReference,
                indexArray(), *arrayType->value, &statementContext);
        }

        default:
            throw;
        }
    }

    builder::Wrapped Scope::makeExpressionNoun(const utils::ExpressionNoun &noun) {
        builder::Wrapped result = makeExpressionNounContent(noun.content);

        for (const hermes::Node *modifier : noun.modifiers)
            result = makeExpressionNounModifier(modifier, result);

        return result;
    }

    builder::Wrapped Scope::makeExpressionOperation(const utils::ExpressionOperation &operation) {
        builder::Result value = infer(makeExpressionResult(*operation.a));

        switch (operation.op->is<parser::Kind>()) {
        case parser::Kind::Unary:
            switch (operation.op->as<parser::Unary>()->op) {
            case utils::UnaryOperation::Not: {
                std::optional<builder::Result> converted
                    = convert(value, utils::PrimitiveTypename { utils::PrimitiveType::Bool });

                if (!converted.has_value())
                    throw VerifyError(operation.op, "Source type for not expression must be convertible to bool.");

                return builder::Result(builder::Result::FlagTemporary,
                    current ? current->CreateNot(get(converted.value())) : nullptr,
                    utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);
            }

            case utils::UnaryOperation::Negative: {
                auto typePrim = std::get_if<utils::PrimitiveTypename>(&value.type);

                if (!typePrim || !(typePrim->isSigned() || typePrim->isFloat()))
                    throw VerifyError(operation.op, "Source type for operation must be signed or float.");

                return builder::Result(builder::Result::FlagTemporary,
                    current ? typePrim->isFloat() ? current->CreateFNeg(get(value)) : current->CreateNeg(get(value))
                            : nullptr,
                    value.type, &statementContext);
            }

            case utils::UnaryOperation::Reference:
                if (!value.isSet(builder::Result::FlagReference))
                    throw VerifyError(operation.op, "Cannot get reference of temporary.");

                return builder::Result(builder::Result::FlagTemporary, value.value,
                    utils::ReferenceTypename { std::make_shared<utils::Typename>(value.type) }, &statementContext);

            case utils::UnaryOperation::Fetch: {
                if (auto refType = std::get_if<utils::ReferenceTypename>(&value.type)) {
                    return builder::Result(
                        builder::Result::FlagReference | (refType->isMutable ? builder::Result::FlagMutable : 0),
                        get(value), *std::get<utils::ReferenceTypename>(value.type).value, &statementContext);
                } else {
                    throw VerifyError(operation.op, "Cannot dereference value of non reference.");
                }
            }

            default:
                throw;
            }

        case parser::Kind::Ternary: {
            std::optional<builder::Result> inferConverted
                = convert(value, utils::PrimitiveTypename { utils::PrimitiveType::Bool });

            if (!inferConverted.has_value()) {
                throw VerifyError(operation.op,
                    "Must be able to be converted to boolean type for "
                    "ternary, instead type is {}.",
                    toString(value.type));
            }

            builder::Result sub = inferConverted.value();

            builder::Scope trueScope(operation.op->children[0]->as<parser::Expression>(), *this, current.has_value());
            builder::Scope falseScope(operation.op->children[1]->as<parser::Expression>(), *this, current.has_value());

            assert(trueScope.product.has_value() && falseScope.product.has_value());

            builder::Result productA = trueScope.product.value();
            builder::Result productB = falseScope.product.value();

            auto results = convert(productA, trueScope, productB, falseScope);

            if (!results.has_value()) {
                throw VerifyError(operation.op,
                    "Branches of ternary of type {} and {} cannot be "
                    "converted to each other.",
                    toString(productA.type), toString(productB.type));
            }

            builder::Result onTrue = results.value().first;
            builder::Result onFalse = results.value().second;

            assert(onTrue.type == onFalse.type);

            llvm::Value *literal = nullptr;

            assert(function);

            if (current) {
                literal = function->entry.CreateAlloca(builder.makeTypename(onTrue.type));

                trueScope.current->CreateStore(trueScope.get(onTrue), literal);
                falseScope.current->CreateStore(falseScope.get(onFalse), literal);

                current->CreateCondBr(get(sub), trueScope.openingBlock, falseScope.openingBlock);

                currentBlock = llvm::BasicBlock::Create(builder.context, "", function->function, lastBlock);
                current->SetInsertPoint(currentBlock);

                trueScope.current->CreateBr(currentBlock);
                falseScope.current->CreateBr(currentBlock);
            }

            return builder::Result(builder::Result::FlagTemporary | builder::Result::FlagReference, literal,
                onTrue.type, &statementContext);
        }

        case parser::Kind::As: {
            auto *e = operation.op->as<parser::As>();

            auto destination = builder.resolveTypename(e->type());

            std::optional<builder::Result> converted = convert(value, destination, true);

            if (!converted) {
                throw VerifyError(
                    operation.op, "Cannot convert type {} to type {}.", toString(value.type), toString(destination));
            }

            return *converted;
        }

        default:
            throw;
        }
    }

    builder::Result Scope::combine(
        const builder::Result &left, const builder::Result &right, utils::BinaryOperation op) {
        auto results = convert(left, right);

        if (!results.has_value()) {
            throw std::runtime_error(fmt::format("Both sides of operator must be convertible to each other, "
                                                 "but got ls type {}, and rs type {}.",
                toString(left.type), toString(right.type)));
        }

        builder::Result a = results->first;
        builder::Result b = results->second;

        using Requirement = std::function<bool()>;

        auto aPrim = std::get_if<utils::PrimitiveTypename>(&a.type);
        auto bPrim = std::get_if<utils::PrimitiveTypename>(&b.type);

        auto asInt = std::make_pair([&]() { return aPrim && bPrim && aPrim->isNumber() && bPrim->isNumber(); }, "int");

        auto asRef = std::make_pair(
            [&]() {
                return std::holds_alternative<utils::ReferenceTypename>(a.type)
                    && std::holds_alternative<utils::ReferenceTypename>(b.type);
            },
            "ref");

        auto needs = [&](const std::vector<std::pair<Requirement, const char *>> &requirements) {
            if (!std::any_of(requirements.begin(), requirements.end(), [](const auto &e) { return e.first(); })) {
                std::vector<std::string> options(requirements.size());

                std::transform(
                    requirements.begin(), requirements.end(), options.begin(), [](const auto &e) { return e.second; });

                // TODO: Converting to desired behavior will provide better everything.
                throw std::runtime_error(fmt::format("Both sides of operator must be {}, but decided type was {}.",
                    fmt::join(options, " or "), toString(a.type), toString(b.type)));
            }
        };

        switch (op) {
        case utils::BinaryOperation::Add:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat() ? current->CreateFAdd(get(a), get(b)) : current->CreateAdd(get(a), get(b))
                        : nullptr,
                a.type, &statementContext);

        case utils::BinaryOperation::Sub:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat() ? current->CreateFSub(get(a), get(b)) : current->CreateSub(get(a), get(b))
                        : nullptr,
                a.type, &statementContext);

        case utils::BinaryOperation::Mul:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat() ? current->CreateFMul(get(a), get(b)) : current->CreateMul(get(a), get(b))
                        : nullptr,
                a.type, &statementContext);

        case utils::BinaryOperation::Div:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat()  ? current->CreateFDiv(get(a), get(b))
                        : aPrim->isSigned() ? current->CreateSDiv(get(a), get(b))
                                            : current->CreateUDiv(get(a), get(b))
                        : nullptr,
                a.type, &statementContext);

        case utils::BinaryOperation::Mod:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat()  ? current->CreateFRem(get(a), get(b))
                        : aPrim->isSigned() ? current->CreateURem(get(a), get(b))
                                            : current->CreateSRem(get(a), get(b))
                        : nullptr,
                a.type, &statementContext);

        case utils::BinaryOperation::Equals:
            needs({ asInt, asRef });

            return builder::Result(builder::Result::FlagTemporary,
                current ? (asRef.first() || !aPrim->isFloat()) ? current->CreateICmpEQ(get(a), get(b))
                                                               : current->CreateFCmpOEQ(get(a), get(b))
                        : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);

        case utils::BinaryOperation::NotEquals:
            needs({ asInt, asRef });

            return builder::Result(builder::Result::FlagTemporary,
                current ? (asRef.first() || !aPrim->isFloat()) ? current->CreateICmpNE(get(a), get(b))
                                                               : current->CreateFCmpONE(get(a), get(b))
                        : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);

        case utils::BinaryOperation::Greater:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat()  ? current->CreateFCmpOGT(get(a), get(b))
                        : aPrim->isSigned() ? current->CreateICmpSGT(get(a), get(b))
                                            : current->CreateICmpUGT(get(a), get(b))
                        : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);

        case utils::BinaryOperation::GreaterEqual:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat()  ? current->CreateFCmpOGE(get(a), get(b))
                        : aPrim->isSigned() ? current->CreateICmpSGE(get(a), get(b))
                                            : current->CreateICmpUGE(get(a), get(b))
                        : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);

        case utils::BinaryOperation::Lesser:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat()  ? current->CreateFCmpOLT(get(a), get(b))
                        : aPrim->isSigned() ? current->CreateICmpSLT(get(a), get(b))
                                            : current->CreateICmpULT(get(a), get(b))
                        : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);

        case utils::BinaryOperation::LesserEqual:
            needs({ asInt });

            return builder::Result(builder::Result::FlagTemporary,
                current ? aPrim->isFloat()  ? current->CreateFCmpOLE(get(a), get(b))
                        : aPrim->isSigned() ? current->CreateICmpSLE(get(a), get(b))
                                            : current->CreateICmpULE(get(a), get(b))
                        : nullptr,
                utils::PrimitiveTypename { utils::PrimitiveType::Bool }, &statementContext);

        default:
            throw std::runtime_error("Unimplemented combinator operator.");
        }
    }

    builder::Wrapped Scope::makeExpressionCombinator(const utils::ExpressionCombinator &combinator) {
        try {
            return combine(infer(makeExpressionResult(*combinator.a)), infer(makeExpressionResult(*combinator.b)),
                combinator.op->op);
        } catch (const std::runtime_error &e) { throw VerifyError(combinator.op, "{}", e.what()); }
    }

    builder::Wrapped Scope::makeExpressionResult(const utils::ExpressionResult &result) {
        struct {
            builder::Scope &scope;

            builder::Wrapped operator()(const utils::ExpressionNoun &result) {
                return scope.makeExpressionNoun(result);
            }

            builder::Wrapped operator()(const utils::ExpressionOperation &result) {
                return scope.makeExpressionOperation(result);
            }

            builder::Wrapped operator()(const utils::ExpressionCombinator &result) {
                return scope.makeExpressionCombinator(result);
            }
        } visitor { *this };

        return std::visit(visitor, result);
    }

    builder::Result Scope::makeExpression(const parser::Expression *node) {
        return infer(makeExpressionResult(node->result));
    }
}
