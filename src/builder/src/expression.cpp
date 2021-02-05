#include <builder/builder.h>

#include <builder/error.h>
#include <builder/lifetime/null.h>
#include <builder/lifetime/multiple.h>
#include <builder/lifetime/reference.h>
#include <builder/lifetime/array.h>

#include <parser/bool.h>
#include <parser/array.h>
#include <parser/number.h>
#include <parser/function.h>
#include <parser/operator.h>
#include <parser/variable.h>
#include <parser/reference.h>

BuilderResult BuilderScope::makeExpressionNounContent(const Node *node) {
    switch (node->is<Kind>()) {
        case Kind::Parentheses:
            return makeExpression(node->children.front()->as<ExpressionNode>()->result);

        case Kind::Reference: {
            const Node *e = Builder::find(node->as<ReferenceNode>());

            switch (e->is<Kind>()) {
                case Kind::Variable: {
                    auto varInfo = findVariable(e->as<VariableNode>());

                    if (!varInfo)
                        throw VerifyError(node, "Cannot find variable reference.");

                    BuilderVariableInfo info = varInfo.value();

                    return BuilderResult(
                        BuilderResult::Kind::Reference,

                        info.variable.value,
                        info.variable.type,

                        1, info.variable.lifetime // depth: 1, some alias lifetime
                    );
                }

                case Kind::Function: {
                    const auto *functionNode = e->as<FunctionNode>();

                    BuilderFunction *callable;

                    auto iterator = function.builder.functions.find(functionNode);
                    if (iterator == function.builder.functions.end()) {
                        auto builderFunction = std::make_unique<BuilderFunction>(functionNode, function.builder);
                        callable = builderFunction.get();

                        function.builder.functions[functionNode] = std::move(builderFunction);
                    } else {
                        callable = iterator->second.get();
                    }

                    return BuilderResult(
                        BuilderResult::Kind::Raw,

                        callable->function,
                        callable->type,

                        0, { }
                    );
                }

                default:
                    assert(false);
            }
        }

        case Kind::Null: {
            return BuilderResult(
                BuilderResult::Kind::Raw,
                ConstantPointerNull::get(Type::getInt8PtrTy(function.builder.context)),
                TypenameNode::null,

                0, std::make_shared<MultipleLifetime>(MultipleLifetime { ReferenceLifetime::null() })
            );
        }

        case Kind::Bool:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                ConstantInt::get(Type::getInt1Ty(function.builder.context), node->as<BoolNode>()->value),
                TypenameNode::boolean
            );

        case Kind::Array: {
            auto *e = node->as<ArrayNode>();

            std::vector<BuilderResult> results;
            results.reserve(e->children.size());

            std::transform(e->children.begin(), e->children.end(), std::back_inserter(results),
                [this](const std::unique_ptr<Node> &x) { return makeExpression(x->as<ExpressionNode>()->result); });

            Typename subType = results.empty() ? TypenameNode::any : results.front().type;

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

            Value *value = function.entry.CreateAlloca(arrayType);

            MultipleLifetime lifetime;

            for (size_t a = 0; a < results.size(); a++) {
                const BuilderResult &result = results[a];

                auto resultLifetimes = flatten(expand({ result.lifetime.get() }, result.lifetimeDepth));
                lifetime.insert(lifetime.end(), resultLifetimes.begin(), resultLifetimes.end());

                Value *index = ConstantInt::get(Type::getInt64Ty(function.builder.context), a);
                Value *point = current.CreateInBoundsGEP(value, {
                    ConstantInt::get(Type::getInt64Ty(function.builder.context), 0),
                    index
                });

                current.CreateStore(get(result), point);
            }

            return BuilderResult(
                BuilderResult::Kind::Literal,
                value,
                type,

                0, std::make_shared<MultipleLifetime>(MultipleLifetime {
                    std::make_shared<ArrayLifetime>(
                        std::vector<std::shared_ptr<MultipleLifetime>> { },
                        std::move(lifetime), PlaceholderId { nullptr, 0 }
                    )
                })
            );
        }

        case Kind::Number: {
            Value *value = ConstantInt::get(Type::getInt32Ty(function.builder.context), node->as<NumberNode>()->value);

            return BuilderResult(
                BuilderResult::Kind::Raw,
                value,
                TypenameNode::integer
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

            if (type->parameters.size() != node->children.size())
                throw VerifyError(node,
                    "Function takes {} arguments, but {} passed.",
                    type->parameters.size(), node->children.size());

            LifetimeMatches lifetimeMatches;

            std::vector<std::vector<MultipleLifetime *>> expandedLifetimes(node->children.size());
            std::vector<Value *> parameters(node->children.size());

            for (size_t a = 0; a < node->children.size(); a++) {
                auto *exp = node->children[a]->as<ExpressionNode>();
                BuilderResult parameter = makeExpression(exp->result);

                std::optional<BuilderResult> parameterConverted = convert(parameter, type->parameters[a]);

                if (!parameterConverted)
                    throw VerifyError(exp,
                        "Expression is being passed to function that expects {} type but got {}.",
                        toString(type->parameters[a]), toString(parameter.type));

                parameter = *parameterConverted;
                parameters[a] = get(parameter);

                auto transform = type->transforms.find(a);

                if (transform != type->transforms.end()) {
                    std::vector<MultipleLifetime *> expanded =
                        expand({ parameter.lifetime.get() }, parameter.lifetimeDepth);

                    join(lifetimeMatches,
                        expanded,
                        *transform->second.initial);

                    expandedLifetimes[a] = std::move(expanded);
                }
            }

            // One more iteration to apply the transforms
            for (size_t a = 0; a < node->children.size(); a++) {
                auto transform = type->transforms.find(a);

                if (transform == type->transforms.end() || !transform->second.final)
                    continue;

                build(lifetimeMatches, expandedLifetimes[a], *transform->second.final);
            }

            std::shared_ptr<MultipleLifetime> resultLifetime = std::make_shared<MultipleLifetime>();
            build(lifetimeMatches, { resultLifetime.get() }, *type->returnTransformFinal);

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateCall(reinterpret_cast<Function *>(result.value), parameters),
                *type->returnType,

                0, std::move(resultLifetime)
            );
        }

        case Kind::Index: {
            // Not get(result.value) we don't want to drop the pointer :|
            // this can't work....

            const ArrayTypename *arrayType = std::get_if<ArrayTypename>(&result.type);

            if (!arrayType) {
                throw VerifyError(node,
                    "Indexing must only be applied on array types, type is {}.",
                    toString(result.type));
            }

            BuilderResult index = makeExpression(node->children.front()->as<ExpressionNode>()->result);
            std::optional<BuilderResult> indexConverted = convert(index, TypenameNode::integer);

            if (!indexConverted.has_value()) {
                throw VerifyError(node->children.front().get(),
                    "Must be able to be converted to int type for indexing, instead type is {}.",
                    toString(index.type));
            }

            index = indexConverted.value();

            return BuilderResult(
                result.kind == BuilderResult::Kind::Reference
                    ? BuilderResult::Kind::Reference : BuilderResult::Kind::Literal,
                current.CreateGEP(ref(result), {
                    ConstantInt::get(Type::getInt64Ty(function.builder.context), 0),
                    get(index)
                }),
                *arrayType->value,

                result.lifetimeDepth + 1, result.lifetime // probably breaks?
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
    BuilderResult value = makeExpression(*operation.a);

    switch (operation.op->op) {
        case UnaryNode::Operation::Not: {
            if (value.type != TypenameNode::boolean)
                throw VerifyError(operation.op, "Source type for not expression must be bool.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateNot(get(value)),
                TypenameNode::boolean
            );
        }

        case UnaryNode::Operation::Reference:
            if (value.kind != BuilderResult::Kind::Reference)
                throw VerifyError(operation.op, "Cannot get reference of temporary.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                value.value,
                ReferenceTypename { std::make_shared<Typename>(value.type) },

                value.lifetimeDepth - 1, value.lifetime // probably breaks?
            );

        case UnaryNode::Operation::Fetch: {
            if (!std::holds_alternative<ReferenceTypename>(value.type))
                throw VerifyError(operation.op, "Cannot dereference value of non reference.");

            std::vector<MultipleLifetime *> resultLifetimes =
                expand({ value.lifetime.get() }, value.lifetimeDepth + 1);

            // I feel like I could combine these two any_ofs but my brain is just ARGH
            auto lifetimeIsNotOkay = [this](MultipleLifetime *x) {
                return !x->resolves(*this);
            };

            auto lifetimeIsEmpty = [](MultipleLifetime *x) {
                return x->empty();
            };

            // Basically, do any of the variables referenced by this expression not exist in scope?
            if (!function.builder.options.noLifetimes) {
                if (std::any_of(resultLifetimes.begin(), resultLifetimes.end(), lifetimeIsNotOkay))
                    throw VerifyError(operation.op, "Cannot dereference value with lifetime that has gone out of scope.");

                if (resultLifetimes.empty() || std::all_of(resultLifetimes.begin(), resultLifetimes.end(), lifetimeIsEmpty))
                    throw VerifyError(operation.op, "Cannot dereference value which may not point to anything.");
            }

            return BuilderResult(
                BuilderResult::Kind::Reference,
                get(value),
                *std::get<ReferenceTypename>(value.type).value,

                value.lifetimeDepth + 1, value.lifetime // probably breaks?
            );
        }

        default:
            assert(false);
    }
}

BuilderResult BuilderScope::makeExpressionCombinator(const ExpressionCombinator &combinator) {
    BuilderResult a = makeExpression(*combinator.a);
    BuilderResult b = makeExpression(*combinator.b);

    // assuming just add stuff for now
    if (a.type != TypenameNode::integer || b.type != TypenameNode::integer) {
        throw VerifyError(combinator.op,
            "Expected ls type int but got {}, rs type int but got {}.",
            toString(a.type), toString(b.type));
    }

    switch (combinator.op->op) {
        case OperatorNode::Operation::Add:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateAdd(get(a), get(b)),
                TypenameNode::integer
            );

        case OperatorNode::Operation::Sub:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateSub(get(a), get(b)),
                TypenameNode::integer
            );

        case OperatorNode::Operation::Mul:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateMul(get(a), get(b)),
                TypenameNode::integer
            );

        case OperatorNode::Operation::Div:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateSDiv(get(a), get(b)),
                TypenameNode::integer
            );

        case OperatorNode::Operation::Equals:
            if (a.type != TypenameNode::integer || b.type != TypenameNode::integer)
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpEQ(get(a), get(b)),
                TypenameNode::boolean
            );

        case OperatorNode::Operation::NotEquals:
            if (a.type != TypenameNode::integer || b.type != TypenameNode::integer)
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpNE(get(a), get(b)),
                TypenameNode::boolean
            );

        case OperatorNode::Operation::Greater:
            if (a.type != TypenameNode::integer || b.type != TypenameNode::integer)
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSGT(get(a), get(b)),
                TypenameNode::boolean
            );

        case OperatorNode::Operation::GreaterEqual:
            if (a.type != TypenameNode::integer || b.type != TypenameNode::integer)
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSGE(get(a), get(b)),
                TypenameNode::boolean
            );

        case OperatorNode::Operation::Lesser:
            if (a.type != TypenameNode::integer || b.type != TypenameNode::integer)
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSLT(get(a), get(b)),
                TypenameNode::boolean
            );

        case OperatorNode::Operation::LesserEqual:
            if (a.type != TypenameNode::integer || b.type != TypenameNode::integer)
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSLE(get(a), get(b)),
                TypenameNode::boolean
            );

        default:
            throw VerifyError(combinator.op, "Unimplemented combinator operator.");
    }
}

BuilderResult BuilderScope::makeExpression(const ExpressionResult &result) {
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