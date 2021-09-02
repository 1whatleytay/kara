#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>
#include <builder/operations.h>

#include <parser/expression.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/operator.h>
#include <parser/type.h>
#include <parser/variable.h>

namespace kara::builder::ops::expression {
    builder::Wrapped makeNounContent(const Context &context, const hermes::Node *node) {
        switch (node->is<parser::Kind>()) {
        case parser::Kind::Parentheses:
            return ops::expression::makeExpression(context, node->as<parser::Parentheses>()->body());

        case parser::Kind::Reference: {
            auto e = node->as<parser::Reference>();

            return Unresolved(e, context.builder.findAll(e));
        }

        case parser::Kind::New: {
            auto *e = node->as<parser::New>();

            return builder::Unresolved(e, { e });
        }

        case parser::Kind::Special:
            return ops::nouns::makeSpecial(context, node->as<parser::Special>()->type);

        case parser::Kind::Bool:
            return ops::nouns::makeBool(context, node->as<parser::Bool>()->value);

        case parser::Kind::String: {
            auto *e = node->as<parser::String>();

            assert(e->inserts.empty());

            return ops::nouns::makeString(context, e->text);
        }

        case parser::Kind::Array: {
            auto *e = node->as<parser::Array>();

            auto elements = e->elements();
            std::vector<builder::Result> values;
            values.reserve(elements.size());

            std::transform(elements.begin(), elements.end(), std::back_inserter(values),
                [&context](auto x) { return ops::expression::makeExpression(context, x); });

            return ops::nouns::makeArray(context, values);
        }

        case parser::Kind::Number:
            return ops::nouns::makeNumber(context, node->as<parser::Number>()->value);

        default:
            throw;
        }
    }

    builder::Wrapped makeNounModifier(const Context &context, const builder::Wrapped &value, const hermes::Node *node) {
        switch (node->is<parser::Kind>()) {
        case parser::Kind::Call: {
            auto callNode = node->as<parser::Call>();

            auto unresolved = std::get_if<builder::Unresolved>(&value);
            if (!unresolved)
                throw VerifyError(node, "Call must be done on unresolved names.");

            ops::matching::MatchInput input;

            input.names = callNode->namesStripped();
            auto parameters = callNode->parameters();

            // Add x in x.y(z, w) to parameter list
            if (unresolved->implicit)
                input.parameters.push_back(*unresolved->implicit);

            // Add z, w in x.y(z, w) to parameter list
            for (auto parameter : parameters)
                input.parameters.push_back(ops::expression::makeExpression(context, parameter));

            auto isNewNode = [](const hermes::Node *n) { return n->is(parser::Kind::New); };
            auto newIt = std::find_if(unresolved->references.begin(), unresolved->references.end(), isNewNode);

            if (newIt != unresolved->references.end()) {
                auto newNode = (*newIt)->as<parser::New>();
                auto type = context.builder.resolveTypename(newNode->type());

                auto typeNode = std::get_if<utils::NamedTypename>(&type);

                if (!typeNode)
                    throw VerifyError(newNode, "New parameters may only be passed to a type/struct.");

                auto wrapped = ops::matching::call(context, { typeNode->type }, input);
                auto returnResult = ops::matching::unwrap(wrapped, unresolved->from);

                auto output = ops::nouns::makeNew(context, type);

                if (context.ir)
                    context.ir->CreateStore(ops::get(context, returnResult), ops::get(context, output));

                return output;
            }

            std::vector<const hermes::Node *> functions;

            auto callable
                = [](const hermes::Node *n) { return n->is(parser::Kind::Function) || n->is(parser::Kind::Type); };

            std::copy_if(
                unresolved->references.begin(), unresolved->references.end(), std::back_inserter(functions), callable);

            if (functions.empty())
                throw VerifyError(node, "Reference did not resolve to any functions to call.");

            return unwrap(ops::matching::call(context, functions, input), unresolved->from);
        }

        case parser::Kind::Dot: {
            builder::Result sub = ops::makeInfer(context, value);

            auto refNode = node->children.front()->as<parser::Reference>();

            // Set up to check if property exists, dereference if needed
            utils::Typename *subtype = &sub.type;

            size_t numReferences = 0;

            while (auto *type = std::get_if<utils::ReferenceTypename>(subtype)) {
                subtype = type->value.get();
                numReferences++;
            }

            if (auto *type = std::get_if<utils::NamedTypename>(subtype)) {
                builder::Type *builderType = context.builder.makeType(type->type);

                auto match = [refNode](auto var) { return var->name == refNode->name; };

                auto fields = type->type->fields();
                auto iterator = std::find_if(fields.begin(), fields.end(), match);

                if (iterator != fields.end()) {
                    auto *varNode = *iterator;

                    if (!varNode->hasFixedType)
                        throw VerifyError(varNode, "All struct variables must have fixed type.");

                    size_t index = builderType->indices.at(varNode);

                    // I feel uneasy touching this...
                    llvm::Value *structRef = numReferences > 0 ? ops::get(context, sub) : ops::ref(context, sub);

                    if (context.ir) {
                        for (size_t a = 1; a < numReferences; a++)
                            structRef = context.ir->CreateLoad(structRef);
                    }

                    return builder::Result {
                        (sub.flags & (builder::Result::FlagMutable | builder::Result::FlagTemporary))
                            | builder::Result::FlagReference,
                        context.ir ? context.ir->CreateStructGEP(structRef, index, refNode->name) : nullptr,
                        context.builder.resolveTypename(varNode->fixedType()),
                        context.accumulator,
                    };
                }
            }

            const auto &global = context.builder.root->children;

            auto matchFunction = [&](const hermes::Node *node) {
                if (!node->is(parser::Kind::Function))
                    return false;

                auto *e = node->as<parser::Function>();
                if (e->name != refNode->name || e->parameterCount == 0)
                    return false;

                return true;
            };

            auto n = context.builder.searchAllDependencies(matchFunction);

            if (n.empty())
                throw VerifyError(node, "Could not find method or field with name {}.", refNode->name);

            return builder::Unresolved(node, n, std::make_unique<builder::Result>(std::move(sub)));
        }

        case parser::Kind::Index: {
            builder::Result sub = ops::makeDereference(context, ops::makeInfer(context, value));

            const utils::ArrayTypename *arrayType = std::get_if<utils::ArrayTypename>(&sub.type);

            if (!arrayType) {
                throw VerifyError(
                    node, "Indexing must only be applied on array types, type is {}.", toString(sub.type));
            }

            auto *indexExpression = node->children.front()->as<parser::Expression>();

            auto ulongType = utils::PrimitiveTypename { utils::PrimitiveType::ULong };

            builder::Result index = ops::expression::makeExpression(context, indexExpression);
            auto indexConverted = ops::makeConvert(context, index, ulongType);

            if (!indexConverted.has_value()) {
                throw VerifyError(indexExpression,
                    "Must be able to be converted to int type for "
                    "indexing, instead type is {}.",
                    toString(index.type));
            }

            index = indexConverted.value();

            auto indexArray = [&]() -> llvm::Value * {
                if (!context.ir)
                    return nullptr;

                switch (arrayType->kind) {
                case utils::ArrayKind::FixedSize:
                    return context.ir->CreateGEP(ops::ref(context, sub),
                        { llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.builder.context), 0),
                            ops::get(context, index) });

                case utils::ArrayKind::Unbounded:
                case utils::ArrayKind::UnboundedSized: // TODO: no good for stack
                    // allocated arrays
                    return context.ir->CreateGEP(ops::ref(context, sub), ops::get(context, index));

                default:
                    throw std::exception();
                }
            };

            return builder::Result {
                (sub.flags & (builder::Result::FlagMutable | builder::Result::FlagTemporary))
                    | builder::Result::FlagReference,
                indexArray(),
                *arrayType->value,
                context.accumulator,
            };
        }

        default:
            throw;
        }
    }

    builder::Wrapped makeNoun(const Context &context, const utils::ExpressionNoun &noun) {
        builder::Wrapped result = ops::expression::makeNounContent(context, noun.content);

        for (const hermes::Node *modifier : noun.modifiers)
            result = ops::expression::makeNounModifier(context, result, modifier);

        return result;
    }

    builder::Wrapped makeOperation(const Context &context, const utils::ExpressionOperation &operation) {
        builder::Result value = ops::makeInfer(context, ops::expression::makeResult(context, *operation.a));

        switch (operation.op->is<parser::Kind>()) {
        case parser::Kind::Unary:
            switch (operation.op->as<parser::Unary>()->op) {
            case utils::UnaryOperation::Not:
                return ops::blame(operation.op, ops::unary::makeNot, context, value);

            case utils::UnaryOperation::Negative:
                return ops::blame(operation.op, ops::unary::makeNegative, context, value);

            case utils::UnaryOperation::Reference:
                return ops::blame(operation.op, ops::unary::makeReference, context, value);

            case utils::UnaryOperation::Fetch:
                return ops::blame(operation.op, ops::unary::makeDereference, context, value);

            default:
                throw;
            }

        case parser::Kind::Ternary: {
            std::optional<builder::Result> inferConverted
                = ops::makeConvert(context, value, utils::PrimitiveTypename { utils::PrimitiveType::Bool });

            if (!inferConverted.has_value()) {
                throw VerifyError(operation.op,
                    "Must be able to be converted to boolean type for "
                    "ternary, instead type is {}.",
                    toString(value.type));
            }

            builder::Result sub = inferConverted.value();

            llvm::BasicBlock *trueBlock = nullptr;
            llvm::BasicBlock *falseBlock = nullptr;

            std::optional<llvm::IRBuilder<>> trueBuilder;
            std::optional<llvm::IRBuilder<>> falseBuilder;

            llvm::BasicBlock *resumeBlock;

            if (context.ir) {
                assert(context.function);

                auto point = context.ir->GetInsertBlock();
                assert(point);

                auto next = point->getNextNode();

                // might have to create my own Accumulator here too
                trueBlock = llvm::BasicBlock::Create(context.builder.context, "", point->getParent(), next);
                falseBlock = llvm::BasicBlock::Create(context.builder.context, "", point->getParent(), next);

                trueBuilder.emplace(trueBlock);
                falseBuilder.emplace(falseBlock);

                resumeBlock = llvm::BasicBlock::Create(context.builder.context, "", point->getParent(), next);
            }

            ops::Context trueContext { context.builder, context.accumulator, trueBuilder ? &*trueBuilder : nullptr,
                context.cache, context.function };

            ops::Context falseContext { context.builder, context.accumulator, falseBuilder ? &*falseBuilder : nullptr,
                context.cache, context.function };

            // might have to work on destruction for true/false
            auto trueExpression = operation.op->children[0]->as<parser::Expression>();
            auto falseExpression = operation.op->children[1]->as<parser::Expression>();

            auto trueValue = ops::expression::makeExpression(trueContext, trueExpression);
            auto falseValue = ops::expression::makeExpression(falseContext, falseExpression);

            auto results = ops::makeConvertExplicit(trueContext, trueValue, falseContext, falseValue);

            if (!results) {
                throw VerifyError(operation.op,
                    "Branches of ternary of type {} and {} cannot be "
                    "converted to each other.",
                    toString(trueValue.type), toString(falseValue.type));
            }

            builder::Result onTrue = results->first;
            builder::Result onFalse = results->second;

            assert(onTrue.type == onFalse.type);

            llvm::Value *literal = nullptr;

            assert(context.function);

            if (context.ir) {
                assert(trueBuilder && falseBuilder);

                literal = context.function->entry.CreateAlloca(context.builder.makeTypename(onTrue.type));

                trueBuilder->CreateStore(ops::get(trueContext, onTrue), literal);
                falseBuilder->CreateStore(ops::get(falseContext, onFalse), literal);

                context.ir->CreateCondBr(ops::get(context, sub), trueBlock, falseBlock);

                context.ir->SetInsertPoint(resumeBlock);

                trueBuilder->CreateBr(resumeBlock);
                falseBuilder->CreateBr(resumeBlock);
            }

            return builder::Result {
                builder::Result::FlagTemporary | builder::Result::FlagReference,
                literal,
                onTrue.type,
                context.accumulator,
            };
        }

        case parser::Kind::As: {
            auto *e = operation.op->as<parser::As>();

            auto destination = context.builder.resolveTypename(e->type());

            auto converted = ops::blame(operation.op, ops::makeConvert, context, value, destination, true);

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

    builder::Wrapped makeCombinator(const Context &context, const utils::ExpressionCombinator &combinator) {
        auto left = ops::makeInfer(context, ops::expression::makeResult(context, *combinator.a));
        auto right = ops::makeInfer(context, ops::expression::makeResult(context, *combinator.b));

        switch (combinator.op->op) {
        case utils::BinaryOperation::Add:
            return ops::blame(combinator.op, ops::binary::makeAdd, context, left, right);

        case utils::BinaryOperation::Sub:
            return ops::blame(combinator.op, ops::binary::makeSub, context, left, right);

        case utils::BinaryOperation::Mul:
            return ops::blame(combinator.op, ops::binary::makeMul, context, left, right);

        case utils::BinaryOperation::Div:
            return ops::blame(combinator.op, ops::binary::makeDiv, context, left, right);

        case utils::BinaryOperation::Mod:
            return ops::blame(combinator.op, ops::binary::makeMod, context, left, right);

        case utils::BinaryOperation::Equals:
            return ops::blame(combinator.op, ops::binary::makeEQ, context, left, right);

        case utils::BinaryOperation::NotEquals:
            return ops::blame(combinator.op, ops::binary::makeNE, context, left, right);

        case utils::BinaryOperation::Greater:
            return ops::blame(combinator.op, ops::binary::makeGT, context, left, right);

        case utils::BinaryOperation::GreaterEqual:
            return ops::blame(combinator.op, ops::binary::makeGE, context, left, right);

        case utils::BinaryOperation::Lesser:
            return ops::blame(combinator.op, ops::binary::makeLT, context, left, right);

        case utils::BinaryOperation::LesserEqual:
            return ops::blame(combinator.op, ops::binary::makeLE, context, left, right);

        case utils::BinaryOperation::Or:
            return ops::blame(combinator.op, ops::binary::makeOr, context, left, right);

        case utils::BinaryOperation::And:
            return ops::blame(combinator.op, ops::binary::makeAnd, context, left, right);

        default:
            throw;
        }
    }

    builder::Wrapped makeResult(const Context &context, const utils::ExpressionResult &result) {
        struct {
            const ops::Context &context;

            builder::Wrapped operator()(const utils::ExpressionNoun &result) {
                return ops::expression::makeNoun(context, result);
            }

            builder::Wrapped operator()(const utils::ExpressionOperation &result) {
                return ops::expression::makeOperation(context, result);
            }

            builder::Wrapped operator()(const utils::ExpressionCombinator &result) {
                return ops::expression::makeCombinator(context, result);
            }
        } visitor { context };

        return std::visit(visitor, result);
    }

    builder::Result makeExpression(const Context &context, const parser::Expression *expression) {
        return ops::makeInfer(context, ops::expression::makeResult(context, expression->result));
    }
}
