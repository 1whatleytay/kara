#include <builder/builder.h>

#include <builder/builtins.h>
#include <builder/error.h>
#include <builder/manager.h>
#include <builder/operations.h>

#include <parser/expression.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/operator.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <builder/handlers.h>

#include <cassert>

namespace kara::builder::ops::modifiers {
    // I actually don't like this, no parser::Call I think
    // I'm going to keep it until it becomes a problem
    //  - it is more inline with the other functions like this
    builder::Wrapped makeCall(const Context &context, const builder::Wrapped &value, const parser::Call *node) {
        // well not especially... f()()
        auto unresolved = std::get_if<builder::Unresolved>(&value);
        if (!unresolved)
            throw VerifyError(node, "Call must be done on unresolved names.");
        // see ^^ we enforce it anyway, problem will have to be solved by future me

        ops::matching::MatchInput input;

        input.names = node->namesStripped();
        auto parameters = node->parameters();

        // Add x in x.y(z, w) to parameter list
        if (unresolved->implicit)
            input.parameters.push_back(*unresolved->implicit);

        // Add z, w in x.y(z, w) to parameter list
        for (auto parameter : parameters)
            input.parameters.push_back(ops::expression::make(context, parameter));

        auto resolve = [&]() {
            // makeCallOnValue
            auto v = handlers::resolve(
                std::array {
                    handlers::makeCallOnNew,
                    handlers::makeCallOnFunctionOrType,
                },
                context, *unresolved, input);

            return *v;
        };

        return ops::blame(node, resolve);
    }

    builder::Wrapped makeDot(const Context &context, const builder::Wrapped &value, const parser::Dot *node) {
        auto infer = [&]() { return ops::makeInfer(context, value); };

        switch (node->children.front()->is<parser::Kind>()) {
        case parser::Kind::Unary:
            return ops::blame(node, ops::expression::makeUnary, context, value, node->unary());

        case parser::Kind::Reference: {
            auto resolve = [&]() {
                auto v = handlers::resolve(
                    std::array {
                        handlers::makeDotForField,
                        handlers::makeDotForUFCS,
                    },
                    context, infer(), node->reference());

                if (!v)
                    die("Could not resolve dot operator.");

                return *v;
            };

            return ops::blame(node, resolve);
        }

        default:
            throw;
        }
    }

    builder::Wrapped makeIndex(const Context &context, const builder::Wrapped &value, const parser::Index *node) {
        builder::Result sub = ops::makeRealType(context, ops::makeInfer(context, value));

        const utils::ArrayTypename *arrayType = std::get_if<utils::ArrayTypename>(&sub.type);

        if (!arrayType) {
            throw VerifyError(node, "Indexing must only be applied on array types, type is {}.", toString(sub.type));
        }

        auto *indexExpression = node->children.front()->as<parser::Expression>();

        auto ulongType = utils::PrimitiveTypename { utils::PrimitiveType::ULong };

        builder::Result index = ops::expression::make(context, indexExpression);
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

            case utils::ArrayKind::VariableSize: {
                auto dataPtr = context.ir->CreateStructGEP(ops::ref(context, sub), 2); // data is at 2

                return context.ir->CreateGEP(context.ir->CreateLoad(dataPtr), ops::get(context, index));
            }

            default:
                throw;
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

    builder::Wrapped makeTernary(const Context &context, const builder::Result &value, const parser::Ternary *node) {
        auto typenameBool = utils::PrimitiveTypename { utils::PrimitiveType::Bool };
        auto inferConverted = ops::makeConvert(context, value, typenameBool);

        if (!inferConverted.has_value()) {
            throw VerifyError(node,
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

        auto trueValue = ops::expression::make(trueContext, node->onTrue());
        auto falseValue = ops::expression::make(falseContext, node->onFalse());

        auto results = ops::makeConvertExplicit(trueContext, trueValue, falseContext, falseValue);

        if (!results) {
            throw VerifyError(node,
                "Branches of ternary of type {} and {} cannot be "
                "converted to each other.",
                toString(trueValue.type), toString(falseValue.type));
        }

        auto [onTrue, onFalse] = *results;

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

    builder::Wrapped makeAs(const Context &context, const builder::Wrapped &value, const parser::As *node) {
        auto result = std::get<builder::Result>(value);

        auto destination = context.builder.resolveTypename(node->type());

        auto converted = ops::blame(node, ops::makeConvert, context, result, destination, true);

        if (!converted) {
            throw VerifyError(node, "Cannot convert type {} to type {}.", toString(result.type), toString(destination));
        }

        return *converted;
    }
}

namespace kara::builder::ops::expression {
    builder::Wrapped makeNounContent(const Context &context, const hermes::Node *node) {
        switch (node->is<parser::Kind>()) {
        case parser::Kind::Parentheses:
            return ops::expression::make(context, node->as<parser::Parentheses>()->body());

        case parser::Kind::Reference: {
            auto e = node->as<parser::Reference>();

            auto builtins = ops::handlers::builtins::matching(e->name);

            // here, search through ops::handlers::builtins::functions and find any that match e->name DONE
            // then we go into Unresolved, change std::vector so it can also contain either std::function or the pointer
            // DONE then we just override a case for that in Call unfortunately this is really rough

            return builder::Unresolved(e, context.builder.findAll(e), std::move(builtins));
        }

        case parser::Kind::New: {
            auto *e = node->as<parser::New>();

            return builder::Unresolved(e, { e }, {});
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
                [&context](auto x) { return ops::expression::make(context, x); });

            return ops::nouns::makeArray(context, values);
        }

        case parser::Kind::Number:
            return ops::nouns::makeNumber(context, node->as<parser::Number>()->value);

        default:
            throw;
        }
    }

//    builder::Wrapped makeNounModifier(const Context &context, const builder::Wrapped &value, const hermes::Node *node) {
//        switch (node->is<parser::Kind>()) {
//
//        default:
//            throw;
//        }
//    }

    builder::Wrapped makeUnary(const Context &context, const builder::Wrapped &wrapped, const parser::Unary *node) {
        auto value = [&context, &wrapped]() { return ops::makeInfer(context, wrapped); };

        switch (node->op) {
        case utils::UnaryOperation::Not:
            return ops::blame(node, ops::unary::makeNot, context, value());

        case utils::UnaryOperation::Negative:
            return ops::blame(node, ops::unary::makeNegative, context, value());

        case utils::UnaryOperation::Reference:
            return ops::blame(node, ops::unary::makeReference, context, wrapped);

        case utils::UnaryOperation::Fetch:
            return ops::blame(node, ops::unary::makeDereference, context, wrapped);

        default:
            throw;
        }
    }

    builder::Wrapped makeNoun(const Context &context, const utils::ExpressionNoun &noun) {
        builder::Wrapped result = ops::expression::makeNounContent(context, noun.content);

//        for (const hermes::Node *modifier : noun.modifiers)
//            result = ops::expression::makeNounModifier(context, result, modifier);

        return result;
    }

    builder::Wrapped makeOperation(const Context &context, const utils::ExpressionOperation &operation) {
        auto wrapped = ops::expression::makeResult(context, *operation.a);

        auto value = [&context, &wrapped]() { return ops::makeInfer(context, wrapped); };

        switch (operation.op->is<parser::Kind>()) {
        case parser::Kind::Unary:
            return ops::expression::makeUnary(context, wrapped, operation.op->as<parser::Unary>());

        case parser::Kind::Ternary:
            return ops::modifiers::makeTernary(context, value(), operation.op->as<parser::Ternary>());

        case parser::Kind::As:
            return ops::modifiers::makeAs(context, value(), operation.op->as<parser::As>());

        case parser::Kind::Slash:
            return value();

        case parser::Kind::Call:
            return ops::modifiers::makeCall(context, wrapped, operation.op->as<parser::Call>());

        case parser::Kind::Dot:
            return ops::modifiers::makeDot(context, wrapped, operation.op->as<parser::Dot>());

        case parser::Kind::Index:
            return ops::modifiers::makeIndex(context, wrapped, operation.op->as<parser::Index>());

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

        case utils::BinaryOperation::Fallback:
            return ops::blame(combinator.op, ops::binary::makeFallback, context, left, right);

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

    builder::Result make(const Context &context, const parser::Expression *expression) {
        auto result = ops::expression::makeResult(context, expression->result);

        // Double make infer/strong infer to allow for calling of result types
        // like if y is a var func ptr, and I just type y it will go -> Unresolved -> Result -> Call Result
        return ops::makeInfer(context, ops::makeInfer(context, result));
    }
}
