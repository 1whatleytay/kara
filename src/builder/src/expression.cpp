#include <builder/builder.h>

#include <builder/error.h>

#include <parser/number.h>
#include <parser/function.h>
#include <parser/operator.h>
#include <parser/variable.h>
#include <parser/reference.h>

Result Builder::makeExpressionNounContent(const Node *node, const Scope &scope) {
    switch (node->is<Kind>()) {
        case Kind::Parentheses:
            return makeExpression(node->children.front()->as<ExpressionNode>()->result, scope);

        case Kind::Reference: {
            const Node *value = find(node->as<ReferenceNode>());

            if (value->is(Kind::Variable)) {
                const auto *variableNode = value->as<VariableNode>();
                Variable variable = makeVariable(variableNode, scope);

                return {
                    variable.isMutable ? Result::Kind::Reference : Result::Kind::Raw,
                    variable.value,
                    variable.type
                };
            } else if (value->is(Kind::Function)) {
                auto e = value->as<FunctionNode>();
                Callable function = makeFunction(e);

                return {
                    Result::Kind::Raw,
                    function.value,
                    *function.type
                };
            } else {
                assert(false);
            }
        }

        case Kind::Number: {
            Value *value = ConstantInt::get(Type::getInt32Ty(context), node->as<NumberNode>()->value);

            return {
                Result::Kind::Raw,
                value,
                TypenameNode::integer
            };
        }

        default:
            assert(false);
    }
}

Result Builder::makeExpressionNounModifier(const Node *node, const Result &result, const Scope &scope) {
    IRBuilder<> builder(scope.current);

    switch (node->is<Kind>()) {
        case Kind::Call: {
            if (!std::holds_alternative<FunctionTypename>(result.type))
                throw VerifyError(node,
                    "Must call a function pointer object, instead called {}.",
                    toString(result.type));

            if (result.kind != Result::Kind::Raw)
                throw VerifyError(node,
                    "Internal strangeness with function pointer object.");

            const auto &type = std::get<FunctionTypename>(result.type);

            if (type.parameters.size() != node->children.size())
                throw VerifyError(node,
                    "Function takes {} arguments, but {} passed.",
                    type.parameters.size(), node->children.size());

            std::vector<Value *> parameters(node->children.size());

            for (size_t a = 0; a < node->children.size(); a++) {
                auto *exp = node->children[a]->as<ExpressionNode>();
                Result parameter = makeExpression(exp->result, scope);

                if (type.parameters[a] != parameter.type)
                    throw VerifyError(exp,
                        "Expression is being passed to function that expects {} type but got {}.",
                        toString(type.parameters[a]), toString(parameter.type));

                parameters[a] = parameter.get(builder);
            }

            return {
                Result::Kind::Raw,
                builder.CreateCall(result.value, parameters),
                *type.returnType
            };
        }

        default:
            assert(false);
    }
}

Result Builder::makeExpressionNoun(const ExpressionNoun &noun, const Scope &scope) {
    Result result = makeExpressionNounContent(noun.content, scope);

    for (const Node *modifier : noun.modifiers)
        result = makeExpressionNounModifier(modifier, result, scope);

    return result;
}

Result Builder::makeExpressionOperation(const ExpressionOperation &operation, const Scope &scope) {
    Result value = makeExpression(*operation.a, scope);

    IRBuilder<> builder(scope.current);

    switch (operation.op->op) {
        case UnaryNode::Operation::At:
            if (value.kind != Result::Kind::Reference)
                throw VerifyError(operation.op, "Cannot get reference of temporary.");

            return {
                Result::Kind::Raw,
                value.value,
                ReferenceTypename { std::make_shared<Typename>(value.type) }
            };

        case UnaryNode::Operation::Fetch:
            if (!std::holds_alternative<ReferenceTypename>(value.type))
                throw VerifyError(operation.op, "Cannot dereference value of non reference.");

            return {
                Result::Kind::Reference,
                value.get(builder),
                *std::get<ReferenceTypename>(value.type).value
            };

        default:
            assert(false);
    }
}

Result Builder::makeExpressionCombinator(const ExpressionCombinator &combinator, const Scope &scope) {
    Result a = makeExpression(*combinator.a, scope);
    Result b = makeExpression(*combinator.b, scope);

    // assuming just add stuff for now
    if (a.type != TypenameNode::integer || b.type != TypenameNode::integer) {
        throw VerifyError(combinator.op,
            "Expected ls type int but got {}, rs type int but got {}.",
            toString(a.type), toString(b.type));
    }

    IRBuilder<> builder(scope.current);

    Value *value;

    switch (combinator.op->op) {
        case OperatorNode::Operation::Add:
            value = builder.CreateAdd(a.get(builder), b.get(builder));
            break;

        case OperatorNode::Operation::Sub:
            value = builder.CreateSub(a.get(builder), b.get(builder));
            break;

        case OperatorNode::Operation::Mul:
            value = builder.CreateMul(a.get(builder), b.get(builder));
            break;

        case OperatorNode::Operation::Div:
            value = builder.CreateSDiv(a.get(builder), b.get(builder));
            break;

        default:
            assert(false);
    }

    return {
        Result::Kind::Raw,
        value,
        TypenameNode::integer
    };
}

Result Builder::makeExpression(const ExpressionResult &result, const Scope &scope) {
    if (std::holds_alternative<ExpressionNoun>(result))
        return makeExpressionNoun(std::get<ExpressionNoun>(result), scope);

    if (std::holds_alternative<ExpressionOperation>(result))
        return makeExpressionOperation(std::get<ExpressionOperation>(result), scope);

    if (std::holds_alternative<ExpressionCombinator>(result))
        return makeExpressionCombinator(std::get<ExpressionCombinator>(result), scope);

    assert(false);
}