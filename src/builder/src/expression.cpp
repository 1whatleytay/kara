#include <builder/builder.h>

#include <builder/error.h>

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
                        info.variable.node->type,

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

        case Kind::Number: {
            Value *value = ConstantInt::get(Type::getInt32Ty(function.builder.context), node->as<NumberNode>()->value);

            return BuilderResult(
                BuilderResult::Kind::Raw,
                value,
                TypenameNode::integer,

                0, std::make_shared<MultipleLifetime>()
            );
        }

        default:
            assert(false);
    }
}

BuilderResult BuilderScope::makeExpressionNounModifier(const Node *node, const BuilderResult &result) {
    switch (node->is<Kind>()) {
        case Kind::Call: {
            if (!std::holds_alternative<FunctionTypename>(result.type))
                throw VerifyError(node,
                    "Must call a function pointer object, instead called {}.",
                    toString(result.type));

            if (result.kind != BuilderResult::Kind::Raw)
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
                BuilderResult parameter = makeExpression(exp->result);

                if (type.parameters[a] != parameter.type)
                    throw VerifyError(exp,
                        "Expression is being passed to function that expects {} type but got {}.",
                        toString(type.parameters[a]), toString(parameter.type));

                parameters[a] = get(parameter);
            }

            return BuilderResult(
                BuilderResult::Kind::Raw,
                current.CreateCall(reinterpret_cast<Function *>(result.value), parameters),
                *type.returnType,

                0, { } // TODO: Implement lifetimes for function returns
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
        case UnaryNode::Operation::At:
            if (value.kind != BuilderResult::Kind::Reference)
                throw VerifyError(operation.op, "Cannot get reference of temporary.");

            return BuilderResult(
                BuilderResult::Kind::Raw,
                value.value,
                ReferenceTypename { std::make_shared<Typename>(value.type) },

                value.lifetimeDepth - 1, value.lifetime // TODO: definitely breaks
            );

        case UnaryNode::Operation::Fetch:
            if (!std::holds_alternative<ReferenceTypename>(value.type))
                throw VerifyError(operation.op, "Cannot dereference value of non reference.");

            return BuilderResult(
                BuilderResult::Kind::Reference,
                get(value),
                *std::get<ReferenceTypename>(value.type).value,

                value.lifetimeDepth + 1, value.lifetime // TODO: definitely breaks
            );

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

    Value *value;

    switch (combinator.op->op) {
        case OperatorNode::Operation::Add:
            value = current.CreateAdd(get(a), get(b));
            break;

        case OperatorNode::Operation::Sub:
            value = current.CreateSub(get(a), get(b));
            break;

        case OperatorNode::Operation::Mul:
            value = current.CreateMul(get(a), get(b));
            break;

        case OperatorNode::Operation::Div:
            value = current.CreateSDiv(get(a), get(b));
            break;

        default:
            throw VerifyError(combinator.op, "Unimplemented combinator operator.");
    }

    return BuilderResult(
        BuilderResult::Kind::Raw,
        value,
        TypenameNode::integer,

        0, std::make_shared<MultipleLifetime>()
    );
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