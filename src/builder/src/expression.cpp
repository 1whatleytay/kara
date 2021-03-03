#include <builder/builder.h>

#include <builder/error.h>

#include <parser/bool.h>
#include <parser/type.h>
#include <parser/array.h>
#include <parser/number.h>
#include <parser/function.h>
#include <parser/operator.h>
#include <parser/variable.h>
#include <parser/reference.h>

BuilderResult BuilderScope::makeExpressionNounContent(const Node *node) {
    switch (node->is<Kind>()) {
        case Kind::Parentheses:
            return makeExpression(node->children.front()->as<ExpressionNode>());

        case Kind::Reference: {
            const Node *e = Builder::find(node->as<ReferenceNode>());

            switch (e->is<Kind>()) {
                case Kind::Variable: {
                    BuilderVariable *info = findVariable(e->as<VariableNode>());

                    if (!info)
                        throw VerifyError(node, "Cannot find variable reference.");

                    return BuilderResult(
                        BuilderResult::Kind::Reference,

                        info->value,
                        info->type
                    );
                }

                case Kind::Function: {
                    const auto *functionNode = e->as<FunctionNode>();

                    BuilderFunction *callable = function.builder.makeFunction(functionNode);

                    return BuilderResult(
                        BuilderResult::Kind::Raw,

                        callable->function,
                        callable->type
                    );
                }

                default:
                    assert(false);
            }
        }

        case Kind::Null: {
            return BuilderResult(
                BuilderResult::Kind::Raw,
                ConstantPointerNull::get(Type::getInt8PtrTy(*function.builder.context)),
                types::null()
            );
        }

        case Kind::Bool:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                ConstantInt::get(Type::getInt1Ty(*function.builder.context), node->as<BoolNode>()->value),
                types::boolean()
            );

        case Kind::Array: {
            auto *e = node->as<ArrayNode>();

            std::vector<BuilderResult> results;
            results.reserve(e->children.size());

            std::transform(e->children.begin(), e->children.end(), std::back_inserter(results),
                [this](const std::unique_ptr<Node> &x) { return makeExpression(x->as<ExpressionNode>()); });

            Typename subType = results.empty() ? types::any() : results.front().type;

            if (!std::all_of(results.begin(), results.end(), [subType](const BuilderResult &result) {
                return result.type == subType;
            })) {
                throw VerifyError(e, "Array elements must all be the same type ({}).", toString(subType));
            }

            ArrayTypename type = {
                ArrayTypename::Kind::FixedSize,
                std::make_shared<Typename>(subType),
                results.size()
            };

            Type *arrayType = function.builder.makeTypename(type);

            Value *value = nullptr;

            if (current) {
                value = function.entry.CreateAlloca(arrayType);

                for (size_t a = 0; a < results.size(); a++) {
                    const BuilderResult &result = results[a];

                    Value *index = ConstantInt::get(Type::getInt64Ty(*function.builder.context), a);
                    Value *point = current->CreateInBoundsGEP(value, {
                        ConstantInt::get(Type::getInt64Ty(*function.builder.context), 0),
                        index
                    });

                    current->CreateStore(get(result), point);
                }
            }

            return BuilderResult(
                BuilderResult::Kind::Literal,
                value,
                type
            );
        }

        case Kind::Number: {
            auto *e = node->as<NumberNode>();

            Type *type = function.builder.makeBuiltinTypename(std::get<StackTypename>(e->type));

            assert(types::isNumber(e->type));

            Value *value;
            if (types::isSigned(e->type))
                value = ConstantInt::getSigned(type, e->value.i);
            if (types::isUnsigned(e->type))
                value = ConstantInt::get(type, e->value.u);
            if (types::isFloat(e->type))
                value = ConstantFP::get(type, e->value.f);

            return BuilderResult(
                BuilderResult::Kind::Raw,
                value,
                e->type
            );
        }

        default:
            assert(false);
    }
}

BuilderResult BuilderScope::makeExpressionNounModifier(const Node *node, const BuilderResult &result) {
    switch (node->is<Kind>()) {
        case Kind::Call: {
            const FunctionTypename *type = std::get_if<FunctionTypename>(&result.type);

            if (!type)
                throw VerifyError(node,
                    "Must call a function pointer object, instead called {}.",
                    toString(result.type));

            if (result.kind != BuilderResult::Kind::Raw)
                throw VerifyError(node,
                    "Internal strangeness with function pointer object.");

            size_t extraParameter = result.implicit ? 1 : 0;

            if (type->parameters.size() != node->children.size() + extraParameter)
                throw VerifyError(node,
                    "Function takes {} arguments, but {} passed.",
                    type->parameters.size(), node->children.size());

            std::vector<Value *> parameters(node->children.size() + extraParameter);

            if (result.implicit)
                parameters[0] = get(*result.implicit);

            for (size_t a = extraParameter; a < node->children.size(); a++) {
                auto *exp = node->children[a]->as<ExpressionNode>();
                BuilderResult parameter = makeExpression(exp);

                std::optional<BuilderResult> parameterConverted = convert(parameter, type->parameters[a]);

                if (!parameterConverted)
                    throw VerifyError(exp,
                        "Expression is being passed to function that expects {} type but got {}.",
                        toString(type->parameters[a]), toString(parameter.type));

                parameter = *parameterConverted;
                parameters[a] = get(parameter);
            }

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current ? current->CreateCall(reinterpret_cast<Function *>(result.value), parameters) : nullptr,
                *type->returnType
            );
        }

        case Kind::Dot: {
            BuilderResult infer = makeExpressionInferred(result);

            const ReferenceNode *refNode = node->children.front()->as<ReferenceNode>();

            // Set up to check if property exists, dereference if needed
            Typename *subtype = &infer.type;

            size_t numReferences = 0;

            while (auto *type = std::get_if<ReferenceTypename>(subtype)) {
                subtype = type->value.get();
                numReferences++;
            }

            const StackTypename *type = std::get_if<StackTypename>(subtype);

            if (!type)
                throw VerifyError(node, "Dot operator must be applied on a stack typename.");

            if (!function.builder.makeBuiltinTypename(*type)) {

                const TypeNode *typeNode = Builder::find(*type);

                if (!typeNode)
                    throw VerifyError(node, "Cannot find type node for stack typename {}.", type->value);

                BuilderType *builderType = function.builder.makeType(typeNode);

                auto match = [refNode](const std::unique_ptr<Node> &node) {
                    return node->is(Kind::Variable) && node->as<VariableNode>()->name == refNode->name;
                };

                auto iterator = std::find_if(typeNode->children.begin(), typeNode->children.end(), match);

                if (iterator != typeNode->children.end()) {
                    const VariableNode *varNode = (*iterator)->as<VariableNode>();

                    if (!varNode->fixedType.has_value())
                        throw VerifyError(varNode, "All struct variables must have fixed type.");

                    size_t index = builderType->indices.at(varNode);

                    // I feel uneasy touching this...
                    Value *structRef = numReferences > 0 ? get(infer) : ref(infer);

                    if (current) {
                        for (size_t a = 1; a < numReferences; a++)
                            structRef = current->CreateLoad(structRef);
                    }

                    return BuilderResult(
                        infer.kind == BuilderResult::Kind::Reference
                            ? BuilderResult::Kind::Reference
                            : BuilderResult::Kind::Literal,
                        current ? current->CreateStructGEP(structRef, index, refNode->name) : nullptr,
                        varNode->fixedType.value()
                    );
                }
            }

            const auto &global = function.builder.root->children;

            // Horrible workaround until I have scopes that don't generate code.
            std::unique_ptr<BuilderResult> converted;

            auto matchFunction = [&](const std::unique_ptr<Node> &node) {
                if (!node->is(Kind::Function))
                    return false;

                auto *e = node->as<FunctionNode>();
                if (e->name != refNode->name || e->parameterCount == 0)
                    return false;

                auto *var = e->children.front()->as<VariableNode>();
                if (!var->fixedType.has_value())
                    return false;

                std::optional<BuilderResult> result = convert(infer, var->fixedType.value());
                if (!result.has_value())
                    return false;

                converted = std::make_unique<BuilderResult>(result.value());

                return true;
            };

            auto funcIterator = std::find_if(global.begin(), global.end(), matchFunction);

            if (funcIterator == global.end())
                throw VerifyError(node, "Could not find method or field with name {}.", refNode->name);

            BuilderFunction *callable = function.builder.makeFunction((*funcIterator)->as<FunctionNode>());

            assert(converted);

            return BuilderResult(
                BuilderResult::Kind::Raw,
                callable->function,
                callable->type,

                std::move(converted)
            );
        }

        case Kind::Index: {
            BuilderResult infer = makeExpressionInferred(result);

            const ArrayTypename *arrayType = std::get_if<ArrayTypename>(&infer.type);

            if (!arrayType) {
                throw VerifyError(node,
                    "Indexing must only be applied on array types, type is {}.",
                    toString(infer.type));
            }

            auto *indexExpression = node->children.front()->as<ExpressionNode>();

            BuilderResult index = makeExpression(indexExpression);
            std::optional<BuilderResult> indexConverted = convert(index, types::i32());

            if (!indexConverted.has_value()) {
                throw VerifyError(indexExpression,
                    "Must be able to be converted to int type for indexing, instead type is {}.",
                    toString(index.type));
            }

            index = indexConverted.value();

            return BuilderResult(
                infer.kind == BuilderResult::Kind::Reference
                    ? BuilderResult::Kind::Reference
                    : BuilderResult::Kind::Literal,
                current ? current->CreateGEP(ref(infer), {
                    ConstantInt::get(Type::getInt64Ty(*function.builder.context), 0),
                    get(index)
                }) : nullptr,
                *arrayType->value
            );
        }

        default:
            assert(false);
    }
}

BuilderResult BuilderScope::makeExpressionNoun(const ExpressionNoun &noun) {
    BuilderResult result = makeExpressionNounContent(noun.content);

    for (const Node *modifier : noun.modifiers)
        result = makeExpressionNounModifier(modifier, result);

    return result;
}

BuilderResult BuilderScope::makeExpressionOperation(const ExpressionOperation &operation) {
    BuilderResult value = makeExpressionResult(*operation.a);

    switch (operation.op->is<Kind>()) {
        case Kind::Unary:
            switch (operation.op->as<UnaryNode>()->op) {
                case UnaryNode::Operation::Not: {
                    std::optional<BuilderResult> converted = convert(value, types::boolean());

                    if (!converted.has_value())
                        throw VerifyError(operation.op, "Source type for not expression must be convertible to bool.");

                    return BuilderResult(
                        BuilderResult::Kind::Raw,
                        current ? current->CreateNot(get(converted.value())) : nullptr,
                        types::boolean()
                    );
                }

                case UnaryNode::Operation::Reference:
                    if (value.kind != BuilderResult::Kind::Reference)
                        throw VerifyError(operation.op, "Cannot get reference of temporary.");

                    return BuilderResult(
                        BuilderResult::Kind::Raw,
                        value.value,
                        ReferenceTypename { std::make_shared<Typename>(value.type) }
                    );

                case UnaryNode::Operation::Fetch: {
                    if (!std::holds_alternative<ReferenceTypename>(value.type))
                        throw VerifyError(operation.op, "Cannot dereference value of non reference.");

                    return BuilderResult(
                        BuilderResult::Kind::Reference,
                        get(value),
                        *std::get<ReferenceTypename>(value.type).value
                    );
                }

                default:
                    assert(false);
            }

        case Kind::Ternary: {
            BuilderResult infer = makeExpressionInferred(value);

            std::optional<BuilderResult> inferConverted = convert(infer, types::boolean());

            if (!inferConverted.has_value()) {
                throw VerifyError(operation.op,
                    "Must be able to be converted to boolean type for ternary, instead type is {}.",
                    toString(infer.type));
            }

            infer = inferConverted.value();

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

            if (current) {
                literal = function.entry.CreateAlloca(function.builder.makeTypename(onTrue.type));

                trueScope.current->CreateStore(trueScope.get(onTrue), literal);
                falseScope.current->CreateStore(falseScope.get(onFalse), literal);

                current->CreateCondBr(get(infer), trueScope.openingBlock, falseScope.openingBlock);

                currentBlock = BasicBlock::Create(
                    *function.builder.context, "", function.function, function.exitBlock);
                current->SetInsertPoint(currentBlock);

                trueScope.current->CreateBr(currentBlock);
                falseScope.current->CreateBr(currentBlock);
            }

            return BuilderResult(
                BuilderResult::Kind::Literal,
                literal,
                onTrue.type
            );
        }

        default:
            assert(false);
    }
}

BuilderResult BuilderScope::makeExpressionCombinator(const ExpressionCombinator &combinator) {
    BuilderResult a = makeExpressionInferred(makeExpressionResult(*combinator.a));
    BuilderResult b = makeExpressionInferred(makeExpressionResult(*combinator.b));

    auto results = convert(a, b);

    if (!results.has_value()) {
        throw VerifyError(combinator.op,
            "Both sides of operator must be convertible to each other, but got ls type {}, and rs type {}.",
            toString(a.type), toString(b.type));
    }

    a = results.value().first;
    b = results.value().second;

    using Requirement = std::function<bool()>;

    auto asInt = std::make_pair([&]() {
        return types::isNumber(a.type) && types::isNumber(b.type);
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
            throw VerifyError(combinator.op,
                "Both sides of operator must be {}, but decided type was {}.",
                fmt::format("{}", fmt::join(options, " or ")), toString(a.type), toString(b.type));
        }
    };

    switch (combinator.op->op) {
        case OperatorNode::Operation::Add:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFAdd(get(a), get(b))
                    : current->CreateAdd(get(a), get(b))
                    : nullptr,
                a.type
            );

        case OperatorNode::Operation::Sub:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFSub(get(a), get(b))
                    : current->CreateSub(get(a), get(b))
                    : nullptr,
                types::i32()
            );

        case OperatorNode::Operation::Mul:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFMul(get(a), get(b))
                    : current->CreateMul(get(a), get(b))
                    : nullptr,
                types::i32()
            );

        case OperatorNode::Operation::Div:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFDiv(get(a), get(b))
                    : types::isSigned(a.type)
                    ? current->CreateSDiv(get(a), get(b))
                    : current->CreateUDiv(get(a), get(b))
                    : nullptr,
                types::i32()
            );

        case OperatorNode::Operation::Equals:
            needs({ asInt, asRef });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? (asRef.first() || !types::isFloat(a.type))
                    ? current->CreateICmpEQ(get(a), get(b))
                    : current->CreateFCmpOEQ(get(a), get(b))
                    : nullptr,
                types::boolean()
            );

        case OperatorNode::Operation::NotEquals:
            needs({ asInt, asRef });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? (asRef.first() || !types::isFloat(a.type))
                    ? current->CreateICmpNE(get(a), get(b))
                    : current->CreateFCmpONE(get(a), get(b))
                    : nullptr,
                types::boolean()
            );

        case OperatorNode::Operation::Greater:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFCmpOGT(get(a), get(b))
                    : types::isSigned(a.type)
                    ? current->CreateICmpSGT(get(a), get(b))
                    : current->CreateICmpUGT(get(a), get(b))
                    : nullptr,
                types::boolean()
            );

        case OperatorNode::Operation::GreaterEqual:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFCmpOGE(get(a), get(b))
                    : types::isSigned(a.type)
                    ? current->CreateICmpSGE(get(a), get(b))
                    : current->CreateICmpUGE(get(a), get(b))
                    : nullptr,
                types::boolean()
            );

        case OperatorNode::Operation::Lesser:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFCmpOLT(get(a), get(b))
                    : types::isSigned(a.type)
                    ? current->CreateICmpSLT(get(a), get(b))
                    : current->CreateICmpULT(get(a), get(b))
                    : nullptr,
                types::boolean()
            );

        case OperatorNode::Operation::LesserEqual:
            needs({ asInt });

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current
                    ? types::isFloat(a.type)
                    ? current->CreateFCmpOLE(get(a), get(b))
                    : types::isSigned(a.type)
                    ? current->CreateICmpSLE(get(a), get(b))
                    : current->CreateICmpULE(get(a), get(b))
                    : nullptr,
                types::boolean()
            );

        default:
            throw VerifyError(combinator.op, "Unimplemented combinator operator.");
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

BuilderResult BuilderScope::makeExpressionInferred(const BuilderResult &result) {
    const FunctionTypename *functionTypename = std::get_if<FunctionTypename>(&result.type);

    if (functionTypename && functionTypename->kind == FunctionTypename::Kind::Pointer) {
        std::vector<Value *> params;

        if (result.implicit)
            params.push_back(get(*result.implicit));

        return BuilderResult(
            BuilderResult::Kind::Raw,
            current ? current->CreateCall(reinterpret_cast<Function *>(result.value), params) : nullptr,
            *functionTypename->returnType
        );
    }

    return result;
}

BuilderResult BuilderScope::makeExpression(const ExpressionNode *node) {
    return makeExpressionInferred(makeExpressionResult(node->result));
}
