#include <builder/builder.h>

#include <builder/error.h>
#include <builder/search.h>

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
            return makeExpression(node->children.front()->as<ExpressionNode>()->result);

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
                ConstantPointerNull::get(Type::getInt8PtrTy(function.builder.context)),
                types::null()
            );
        }

        case Kind::Bool:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                ConstantInt::get(Type::getInt1Ty(function.builder.context), node->as<BoolNode>()->value),
                types::boolean()
            );

        case Kind::Array: {
            auto *e = node->as<ArrayNode>();

            std::vector<BuilderResult> results;
            results.reserve(e->children.size());

            std::transform(e->children.begin(), e->children.end(), std::back_inserter(results),
                [this](const std::unique_ptr<Node> &x) { return makeExpression(x->as<ExpressionNode>()->result); });

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

            Type *arrayType = function.builder.makeTypename(type, node);

            Value *value = function.entry.CreateAlloca(arrayType);

            for (size_t a = 0; a < results.size(); a++) {
                const BuilderResult &result = results[a];

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
                type
            );
        }

        case Kind::Number: {
            Value *value = ConstantInt::get(Type::getInt32Ty(function.builder.context), node->as<NumberNode>()->value);

            return BuilderResult(
                BuilderResult::Kind::Raw,
                value,
                types::integer()
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

            std::vector<Value *> parameters(node->children.size());

            for (size_t a = 0; a < node->children.size(); a++) {
                auto *exp = node->children[a]->as<ExpressionNode>();
                BuilderResult parameter = makeExpression(exp->result);

                std::optional<BuilderResult> parameterConverted = convert(parameter, type->parameters[a], exp);

                if (!parameterConverted)
                    throw VerifyError(exp,
                        "Expression is being passed to function that expects {} type but got {}.",
                        toString(type->parameters[a]), toString(parameter.type));

                parameter = *parameterConverted;
                parameters[a] = get(parameter);
            }

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateCall(reinterpret_cast<Function *>(result.value), parameters),
                *type->returnType
            );
        }

        case Kind::Dot: {
            const ReferenceNode *refNode = node->children.front()->as<ReferenceNode>();

            const StackTypename *type = std::get_if<StackTypename>(&result.type);

            if (!type)
                throw VerifyError(node, "Dot operator can only be applied to stack typename at the moment.");

            const TypeNode *typeNode = Builder::find(*type, node);

            if (!typeNode)
                throw VerifyError(node, "Cannot find type node for stack typename.");

            BuilderType *builderType = function.builder.makeType(typeNode);

            auto match = [refNode](const std::unique_ptr<Node> &node) {
                return node->is(Kind::Variable) && node->as<VariableNode>()->name == refNode->name;
            };

            auto iterator = std::find_if(typeNode->children.begin(), typeNode->children.end(), match);

            if (iterator == typeNode->children.end())
                throw VerifyError(node, "Type {} does not have child {}.", typeNode->name, refNode->name);

            const VariableNode *varNode = (*iterator)->as<VariableNode>();

            if (!varNode->fixedType.has_value())
                throw VerifyError(varNode, "All struct variables must have fixed type.");

            size_t index = builderType->indices.at(varNode);

            return BuilderResult(
                result.kind == BuilderResult::Kind::Reference
                    ? BuilderResult::Kind::Reference
                    : BuilderResult::Kind::Literal,
                current.CreateStructGEP(ref(result, node), index, refNode->name),
                varNode->fixedType.value()
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
            std::optional<BuilderResult> indexConverted = convert(index, types::integer(), node);

            if (!indexConverted.has_value()) {
                throw VerifyError(node->children.front().get(),
                    "Must be able to be converted to int type for indexing, instead type is {}.",
                    toString(index.type));
            }

            index = indexConverted.value();

            return BuilderResult(
                result.kind == BuilderResult::Kind::Reference
                    ? BuilderResult::Kind::Reference : BuilderResult::Kind::Literal,
                current.CreateGEP(ref(result, node), {
                    ConstantInt::get(Type::getInt64Ty(function.builder.context), 0),
                    get(index)
                }),
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
    BuilderResult value = makeExpression(*operation.a);

    switch (operation.op->op) {
        case UnaryNode::Operation::Not: {
            if (value.type != types::boolean())
                throw VerifyError(operation.op, "Source type for not expression must be bool.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateNot(get(value)),
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
}

BuilderResult BuilderScope::makeExpressionCombinator(const ExpressionCombinator &combinator) {
    BuilderResult a = makeExpression(*combinator.a);
    BuilderResult b = makeExpression(*combinator.b);

    // assuming just add stuff for now
    if (a.type != types::integer() || b.type != types::integer()) {
        throw VerifyError(combinator.op,
            "Expected ls type int but got {}, rs type int but got {}.",
            toString(a.type), toString(b.type));
    }

    switch (combinator.op->op) {
        case OperatorNode::Operation::Add:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateAdd(get(a), get(b)),
                types::integer()
            );

        case OperatorNode::Operation::Sub:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateSub(get(a), get(b)),
                types::integer()
            );

        case OperatorNode::Operation::Mul:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateMul(get(a), get(b)),
                types::integer()
            );

        case OperatorNode::Operation::Div:
            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateSDiv(get(a), get(b)),
                types::integer()
            );

        case OperatorNode::Operation::Equals:
            if (a.type != types::integer() || b.type != types::integer())
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpEQ(get(a), get(b)),
                types::boolean()
            );

        case OperatorNode::Operation::NotEquals:
            if (a.type != types::integer() || b.type != types::integer())
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpNE(get(a), get(b)),
                types::boolean()
            );

        case OperatorNode::Operation::Greater:
            if (a.type != types::integer() || b.type != types::integer())
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSGT(get(a), get(b)),
                types::boolean()
            );

        case OperatorNode::Operation::GreaterEqual:
            if (a.type != types::integer() || b.type != types::integer())
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSGE(get(a), get(b)),
                types::boolean()
            );

        case OperatorNode::Operation::Lesser:
            if (a.type != types::integer() || b.type != types::integer())
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSLT(get(a), get(b)),
                types::boolean()
            );

        case OperatorNode::Operation::LesserEqual:
            if (a.type != types::integer() || b.type != types::integer())
                throw VerifyError(combinator.op, "Left side and right side to expression must be int.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateICmpSLE(get(a), get(b)),
                types::boolean()
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