#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>

#include <parser/type.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/operator.h>
#include <parser/variable.h>

BuilderResult BuilderScope::makeExpressionNounContent(const Node *node) {
    switch (node->is<Kind>()) {
        case Kind::Parentheses:
            return makeExpression(node->as<ParenthesesNode>()->body());

        case Kind::Reference: {
            auto e = node->as<ReferenceNode>();

            return BuilderResult(e, builder.findAll(e), &statementContext);
        }

        case Kind::Special: {
            assert(node->as<SpecialNode>()->type == SpecialNode::Type::Null);

            return BuilderResult(
                BuilderResult::FlagTemporary,
                ConstantPointerNull::get(Type::getInt8PtrTy(builder.context)),
                PrimitiveTypename { PrimitiveType::Null },
                &statementContext
            );
        }

        case Kind::Bool:
            return BuilderResult(
                BuilderResult::FlagTemporary,
                ConstantInt::get(Type::getInt1Ty(builder.context), node->as<BoolNode>()->value),
                PrimitiveTypename { PrimitiveType::Bool },
                &statementContext
            );

        case Kind::String: {
            auto *e = node->as<StringNode>();

            assert(e->inserts.empty());

            Value *ptr = nullptr;

            if (current) {
                Constant *initial = ConstantDataArray::getString(builder.context, e->text);

                std::string convertedText(e->text.size(), '.');

                std::transform(e->text.begin(), e->text.end(), convertedText.begin(), [](char c) {
                    return (std::isalpha(c) || std::isdigit(c)) ? c : '_';
                });

                auto variable = new GlobalVariable(
                    *builder.module, initial->getType(),
                    true, GlobalVariable::LinkageTypes::PrivateLinkage,
                    initial, fmt::format("str_{}", convertedText)
                );

                ptr = current->CreateStructGEP(variable, 0);
            }

            return BuilderResult(
                BuilderResult::FlagTemporary,
                ptr,

                ReferenceTypename {
                    std::make_shared<Typename>(ArrayTypename {
                        ArrayKind::Unbounded,

                        std::make_shared<Typename>(PrimitiveTypename { PrimitiveType::Byte })
                    }),

                    false
                },

                &statementContext
            );
        }

        case Kind::Array: {
            auto *e = node->as<ArrayNode>();

            auto elements = e->elements();
            std::vector<BuilderResult> results;
            results.reserve(elements.size());

            std::transform(elements.begin(), elements.end(), std::back_inserter(results),
                [this](auto x) { return makeExpression(x); });

            Typename subType = results.empty() ? PrimitiveTypename::from(PrimitiveType::Any) : results.front().type;

            if (!std::all_of(results.begin(), results.end(), [&subType](const BuilderResult &result) {
                return result.type == subType;
            })) {
                throw VerifyError(e, "Array elements must all be the same type ({}).", toString(subType));
            }

            ArrayTypename type = {
                ArrayKind::FixedSize,
                std::make_shared<Typename>(subType),
                results.size()
            };

            Type *arrayType = builder.makeTypename(type);

            Value *value = nullptr;

            if (current) {
                assert(function);

                value = function->entry.CreateAlloca(arrayType);

                for (size_t a = 0; a < results.size(); a++) {
                    const BuilderResult &result = results[a];

                    Value *index = ConstantInt::get(Type::getInt64Ty(builder.context), a);
                    Value *point = current->CreateInBoundsGEP(value, {
                        ConstantInt::get(Type::getInt64Ty(builder.context), 0),
                        index
                    });

                    current->CreateStore(get(result), point);
                }
            }

            return BuilderResult(
                BuilderResult::FlagTemporary | BuilderResult::FlagReference,
                value,
                type,
                &statementContext
            );
        }

        case Kind::Number: {
            auto *e = node->as<NumberNode>();

            struct {
                LLVMContext &context;
                BuilderStatementContext *statementContext;

                BuilderResult operator()(int64_t s) {
                    return BuilderResult {
                        BuilderResult::FlagTemporary,
                        ConstantInt::getSigned(Type::getInt64Ty(context), s),
                        PrimitiveTypename { PrimitiveType::Long },
                        statementContext
                    };
                }

                BuilderResult operator()(uint64_t u) {
                    return BuilderResult {
                        BuilderResult::FlagTemporary,
                        ConstantInt::get(Type::getInt64Ty(context), u),
                        PrimitiveTypename { PrimitiveType::ULong },
                        statementContext
                    };
                }

                BuilderResult operator()(double f) {
                    return BuilderResult {
                        BuilderResult::FlagTemporary,
                        ConstantFP::get(Type::getDoubleTy(context), f),
                        PrimitiveTypename { PrimitiveType::Double },
                        statementContext
                    };
                }
            } visitor { builder.context, &statementContext };

            return std::visit(visitor, e->value);
        }

        case Kind::New: {
            auto *e = node->as<NewNode>();

            return BuilderResult(e, { e }, &statementContext);
        }

        default:
            throw;
    }
}

BuilderResult BuilderScope::makeExpressionNounModifier(const Node *node, const BuilderResult &result) {
    switch (node->is<Kind>()) {
        case Kind::Call: {
            auto callNode = node->as<CallNode>();

            auto callNames = callNode->names();
            auto callParameters = callNode->parameters();

            assert(!result.isSet(BuilderResult::FlagUnresolved));

            size_t extraParameter = result.implicit ? 1 : 0;

            MatchInput callInput;
            callInput.parameters.resize(callParameters.size() + extraParameter);

            if (result.implicit)
                callInput.parameters[0] = result.implicit.get();

            std::vector<BuilderResult> resultLifetimes;
            resultLifetimes.reserve(callParameters.size());

            for (auto c : callParameters)
                resultLifetimes.push_back(makeExpression(c));

            for (size_t a = 0; a < resultLifetimes.size(); a++)
                callInput.parameters[a + extraParameter] = &resultLifetimes[a];

            callInput.names = callNode->namesStripped();

            auto isNewNode = [](const Node *n) { return n->is(Kind::New); };
            auto newIt = std::find_if(result.references.begin(), result.references.end(), isNewNode);

            if (newIt != result.references.end()) {
                auto newNode = (*newIt)->as<NewNode>();
                auto type = builder.resolveTypename(newNode->type());

                auto typeNode = std::get_if<NamedTypename>(&type);

                if (!typeNode)
                    throw VerifyError(newNode, "New parameters may only be passed to a type/struct.");

                auto value = callUnpack(call({ typeNode->type }, callInput), result.from);

                auto output = makeNew(newNode);

                if (current)
                    current->CreateStore(get(value), get(output));

                return output;
            }

            std::vector<const Node *> functions;

            auto callable = [](const Node *n) { return n->is(Kind::Function) || n->is(Kind::Type); };

            std::copy_if(result.references.begin(), result.references.end(),
                std::back_inserter(functions), callable);

            if (functions.empty())
                throw VerifyError(node, "Reference did not resolve to any functions to call.");

            return callUnpack(call(functions, callInput), result.from);
        }

        case Kind::Dot: {
            BuilderResult sub = infer(result);

            const ReferenceNode *refNode = node->children.front()->as<ReferenceNode>();

            // Set up to check if property exists, dereference if needed
            Typename *subtype = &sub.type;

            size_t numReferences = 0;

            while (auto *type = std::get_if<ReferenceTypename>(subtype)) {
                subtype = type->value.get();
                numReferences++;
            }

            if (auto *type = std::get_if<NamedTypename>(subtype)) {
                BuilderType *builderType = builder.makeType(type->type);

                auto match = [refNode](auto var) { return var->name == refNode->name; };

                auto fields = type->type->fields();
                auto iterator = std::find_if(fields.begin(), fields.end(), match);

                if (iterator != fields.end()) {
                    auto *varNode = *iterator;

                    if (!varNode->hasFixedType)
                        throw VerifyError(varNode, "All struct variables must have fixed type.");

                    size_t index = builderType->indices.at(varNode);

                    // I feel uneasy touching this...
                    Value *structRef = numReferences > 0 ? get(sub) : ref(sub);

                    if (current) {
                        for (size_t a = 1; a < numReferences; a++)
                            structRef = current->CreateLoad(structRef);
                    }

                    return BuilderResult(
                        (sub.flags & (BuilderResult::FlagMutable | BuilderResult::FlagTemporary))
                            | BuilderResult::FlagReference,
                        current ? current->CreateStructGEP(structRef, index, refNode->name) : nullptr,
                        builder.resolveTypename(varNode->fixedType()),
                        &statementContext
                    );
                }
            }

            const auto &global = builder.root->children;

            auto matchFunction = [&](const Node *node) {
                if (!node->is(Kind::Function))
                    return false;

                auto *e = node->as<FunctionNode>();
                if (e->name != refNode->name || e->parameterCount == 0)
                    return false;

                return true;
            };

            auto n = builder.searchAllDependencies(matchFunction);

            if (n.empty())
                throw VerifyError(node, "Could not find method or field with name {}.", refNode->name);

            return BuilderResult(node, n, &statementContext, std::make_unique<BuilderResult>(sub));
        }

        case Kind::Index: {
            BuilderResult sub = unpack(infer(result));

            const ArrayTypename *arrayType = std::get_if<ArrayTypename>(&sub.type);

            if (!arrayType) {
                throw VerifyError(node,
                    "Indexing must only be applied on array types, type is {}.",
                    toString(sub.type));
            }

            auto *indexExpression = node->children.front()->as<ExpressionNode>();

            BuilderResult index = makeExpression(indexExpression);
            std::optional<BuilderResult> indexConverted = convert(index, PrimitiveTypename { PrimitiveType::ULong });

            if (!indexConverted.has_value()) {
                throw VerifyError(indexExpression,
                    "Must be able to be converted to int type for indexing, instead type is {}.",
                    toString(index.type));
            }

            index = indexConverted.value();

            auto indexArray = [&]() -> Value * {
                if (!current)
                    return nullptr;

                switch (arrayType->kind) {
                    case ArrayKind::FixedSize:
                        return current->CreateGEP(ref(sub), {
                            ConstantInt::get(Type::getInt64Ty(builder.context), 0),
                            get(index)
                        });

                    case ArrayKind::Unbounded:
                    case ArrayKind::UnboundedSized: // TODO: no good for stack allocated arrays
                        return current->CreateGEP(ref(sub), get(index));

                    default:
                        throw std::exception();
                }
            };

            return BuilderResult(
                (sub.flags & (BuilderResult::FlagMutable | BuilderResult::FlagTemporary))
                    | BuilderResult::FlagReference,
                indexArray(),
                *arrayType->value,
                &statementContext
            );
        }

        default:
            throw;
    }
}

BuilderResult BuilderScope::makeExpressionNoun(const ExpressionNoun &noun) {
    BuilderResult result = makeExpressionNounContent(noun.content);

    for (const Node *modifier : noun.modifiers)
        result = makeExpressionNounModifier(modifier, result);

    return result;
}

BuilderResult BuilderScope::makeExpressionOperation(const ExpressionOperation &operation) {
    BuilderResult value = infer(makeExpressionResult(*operation.a));

    switch (operation.op->is<Kind>()) {
        case Kind::Unary:
            switch (operation.op->as<UnaryNode>()->op) {
                case UnaryNode::Operation::Not: {
                    std::optional<BuilderResult> converted = convert(value, PrimitiveTypename { PrimitiveType::Bool });

                    if (!converted.has_value())
                        throw VerifyError(operation.op, "Source type for not expression must be convertible to bool.");

                    return BuilderResult(
                        BuilderResult::FlagTemporary,
                        current ? current->CreateNot(get(converted.value())) : nullptr,
                        PrimitiveTypename { PrimitiveType::Bool },
                        &statementContext
                    );
                }

                case UnaryNode::Operation::Negative: {
                    auto typePrim = std::get_if<PrimitiveTypename>(&value.type);

                    if (!typePrim || !(typePrim->isSigned() || typePrim->isFloat()))
                        throw VerifyError(operation.op, "Source type for operation must be signed or float.");

                    return BuilderResult(
                        BuilderResult::FlagTemporary,
                        current
                            ? typePrim->isFloat()
                                ? current->CreateFNeg(get(value))
                                : current->CreateNeg(get(value))
                            : nullptr,
                        value.type,
                        &statementContext
                    );
                }

                case UnaryNode::Operation::Reference:
                    if (!value.isSet(BuilderResult::FlagReference))
                        throw VerifyError(operation.op, "Cannot get reference of temporary.");

                    return BuilderResult(
                        BuilderResult::FlagTemporary,
                        value.value,
                        ReferenceTypename { std::make_shared<Typename>(value.type) },
                        &statementContext
                    );

                case UnaryNode::Operation::Fetch: {
                    if (auto refType = std::get_if<ReferenceTypename>(&value.type)) {
                        return BuilderResult(
                            BuilderResult::FlagReference | (refType->isMutable ? BuilderResult::FlagMutable : 0),
                            get(value),
                            *std::get<ReferenceTypename>(value.type).value,
                            &statementContext
                            );
                    } else {
                        throw VerifyError(operation.op, "Cannot dereference value of non reference.");
                    }
                }

                default:
                    throw;
            }

        case Kind::Ternary: {
            std::optional<BuilderResult> inferConverted = convert(value, PrimitiveTypename { PrimitiveType::Bool });

            if (!inferConverted.has_value()) {
                throw VerifyError(operation.op,
                    "Must be able to be converted to boolean type for ternary, instead type is {}.",
                    toString(value.type));
            }

            BuilderResult sub = inferConverted.value();

            BuilderScope trueScope(operation.op->children[0]->as<ExpressionNode>(), *this, current.has_value());
            BuilderScope falseScope(operation.op->children[1]->as<ExpressionNode>(), *this, current.has_value());

            assert(trueScope.product.has_value() && falseScope.product.has_value());

            BuilderResult productA = trueScope.product.value();
            BuilderResult productB = falseScope.product.value();

            auto results = convert(productA, trueScope, productB, falseScope);

            if (!results.has_value()) {
                throw VerifyError(operation.op,
                    "Branches of ternary of type {} and {} cannot be converted to each other.",
                    toString(productA.type), toString(productB.type));
            }

            BuilderResult onTrue = results.value().first;
            BuilderResult onFalse = results.value().second;

            assert(onTrue.type == onFalse.type);

            Value *literal = nullptr;

            assert(function);

            if (current) {
                literal = function->entry.CreateAlloca(builder.makeTypename(onTrue.type));

                trueScope.current->CreateStore(trueScope.get(onTrue), literal);
                falseScope.current->CreateStore(falseScope.get(onFalse), literal);

                current->CreateCondBr(get(sub), trueScope.openingBlock, falseScope.openingBlock);

                currentBlock = BasicBlock::Create(
                    builder.context, "", function->function, lastBlock);
                current->SetInsertPoint(currentBlock);

                trueScope.current->CreateBr(currentBlock);
                falseScope.current->CreateBr(currentBlock);
            }

            return BuilderResult(
                BuilderResult::FlagTemporary | BuilderResult::FlagReference,
                literal,
                onTrue.type,
                &statementContext
            );
        }

        case Kind::As: {
            auto *e = operation.op->as<AsNode>();

            auto destination = builder.resolveTypename(e->type());

            std::optional<BuilderResult> converted = convert(value, destination, true);

            if (!converted) {
                throw VerifyError(operation.op,
                    "Cannot convert type {} to type {}.",
                    toString(value.type), toString(destination));
            }

            return *converted;
        }

        default:
            throw;
    }
}

BuilderResult BuilderScope::combine(const BuilderResult &left, const BuilderResult &right, OperatorNode::Operation op) {
    auto results = convert(infer(left), infer(right))
        ;

    if (!results.has_value()) {
        throw std::runtime_error(fmt::format(
            "Both sides of operator must be convertible to each other, but got ls type {}, and rs type {}.",
            toString(left.type), toString(right.type)));
    }

    BuilderResult a = results->first;
    BuilderResult b = results->second;

    using Requirement = std::function<bool()>;

    auto aPrim = std::get_if<PrimitiveTypename>(&a.type);
    auto bPrim = std::get_if<PrimitiveTypename>(&b.type);

    auto asInt = std::make_pair([&]() {
        return aPrim && bPrim && aPrim->isNumber() && bPrim->isNumber();
    }, "int");

    auto asRef = std::make_pair([&]() {
        return std::holds_alternative<ReferenceTypename>(a.type) && std::holds_alternative<ReferenceTypename>(b.type);
    }, "ref");

    auto needs = [&](const std::vector<std::pair<Requirement, const char *>> &requirements) {
        if (!std::any_of(requirements.begin(), requirements.end(), [](const auto &e) {
            return e.first();
        })) {
            std::vector<std::string> options(requirements.size());

            std::transform(requirements.begin(), requirements.end(), options.begin(), [](const auto &e) {
                return e.second;
            });

            // TODO: Converting to desired behavior will provide better everything.
            throw std::runtime_error(fmt::format(
                "Both sides of operator must be {}, but decided type was {}.",
                fmt::join(options, " or "), toString(a.type), toString(b.type)));
        }
    };

    switch (op) {
        case OperatorNode::Operation::Add:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFAdd(get(a), get(b))
                    : current->CreateAdd(get(a), get(b))
                    : nullptr,
                a.type,
                &statementContext
            );

        case OperatorNode::Operation::Sub:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFSub(get(a), get(b))
                    : current->CreateSub(get(a), get(b))
                    : nullptr,
                a.type,
                &statementContext
            );

        case OperatorNode::Operation::Mul:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFMul(get(a), get(b))
                    : current->CreateMul(get(a), get(b))
                    : nullptr,
                a.type,
                &statementContext
            );

        case OperatorNode::Operation::Div:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFDiv(get(a), get(b))
                    : aPrim->isSigned()
                        ? current->CreateSDiv(get(a), get(b))
                        : current->CreateUDiv(get(a), get(b))
                    : nullptr,
                a.type,
                &statementContext
            );

        case OperatorNode::Operation::Mod:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFRem(get(a), get(b))
                    : aPrim->isSigned()
                        ? current->CreateURem(get(a), get(b))
                        : current->CreateSRem(get(a), get(b))
                    : nullptr,
                a.type,
                &statementContext
            );

        case OperatorNode::Operation::Equals:
            needs({ asInt, asRef });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? (asRef.first() || !aPrim->isFloat())
                    ? current->CreateICmpEQ(get(a), get(b))
                    : current->CreateFCmpOEQ(get(a), get(b))
                    : nullptr,
                PrimitiveTypename { PrimitiveType::Bool },
                &statementContext
            );

        case OperatorNode::Operation::NotEquals:
            needs({ asInt, asRef });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? (asRef.first() || !aPrim->isFloat())
                    ? current->CreateICmpNE(get(a), get(b))
                    : current->CreateFCmpONE(get(a), get(b))
                    : nullptr,
                PrimitiveTypename { PrimitiveType::Bool },
                &statementContext
            );

        case OperatorNode::Operation::Greater:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFCmpOGT(get(a), get(b))
                    : aPrim->isSigned()
                        ? current->CreateICmpSGT(get(a), get(b))
                        : current->CreateICmpUGT(get(a), get(b))
                    : nullptr,
                PrimitiveTypename { PrimitiveType::Bool },
                &statementContext
            );

        case OperatorNode::Operation::GreaterEqual:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFCmpOGE(get(a), get(b))
                    : aPrim->isSigned()
                        ? current->CreateICmpSGE(get(a), get(b))
                        : current->CreateICmpUGE(get(a), get(b))
                    : nullptr,
                PrimitiveTypename { PrimitiveType::Bool },
                &statementContext
            );

        case OperatorNode::Operation::Lesser:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFCmpOLT(get(a), get(b))
                    : aPrim->isSigned()
                        ? current->CreateICmpSLT(get(a), get(b))
                        : current->CreateICmpULT(get(a), get(b))
                    : nullptr,
                PrimitiveTypename { PrimitiveType::Bool },
                &statementContext
            );

        case OperatorNode::Operation::LesserEqual:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::FlagTemporary,
                current
                    ? aPrim->isFloat()
                    ? current->CreateFCmpOLE(get(a), get(b))
                    : aPrim->isSigned()
                        ? current->CreateICmpSLE(get(a), get(b))
                        : current->CreateICmpULE(get(a), get(b))
                    : nullptr,
                PrimitiveTypename { PrimitiveType::Bool },
                &statementContext
            );

        default:
            throw std::runtime_error("Unimplemented combinator operator.");
    }
}

BuilderResult BuilderScope::makeExpressionCombinator(const ExpressionCombinator &combinator) {
    try {
        return combine(
            makeExpressionResult(*combinator.a),
            makeExpressionResult(*combinator.b),
            combinator.op->op);
    } catch (const std::runtime_error &e) {
        throw VerifyError(combinator.op, "{}", e.what());
    }
}

BuilderResult BuilderScope::makeExpressionResult(const ExpressionResult &result) {
    struct {
        BuilderScope &scope;

        BuilderResult operator()(const ExpressionNoun &result) {
            return scope.makeExpressionNoun(result);
        }

        BuilderResult operator()(const ExpressionOperation &result) {
            return scope.makeExpressionOperation(result);
        }

        BuilderResult operator()(const ExpressionCombinator &result) {
            return scope.makeExpressionCombinator(result);
        }
    } visitor { *this };

    return std::visit(visitor, result);
}

BuilderResult BuilderScope::makeExpression(const ExpressionNode *node) {
    return infer(makeExpressionResult(node->result));
}
