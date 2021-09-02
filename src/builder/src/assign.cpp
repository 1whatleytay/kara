#include <builder/builder.h>

#include <builder/error.h>
#include <builder/manager.h>
#include <builder/operations.h>

#include <parser/assign.h>
#include <parser/expression.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/variable.h>

namespace kara::builder {
    //    void Scope::invokeDestroy(const builder::Result &result) { invokeDestroy(result, exitChainBegin); }
    //
    //    void Scope::invokeDestroy(const builder::Result &result, llvm::BasicBlock *block) {
    //        llvm::IRBuilder<> irBuilder(builder.context);
    //        irBuilder.SetInsertPoint(block, block->begin());
    //
    //        invokeDestroy(result, irBuilder);
    //    }

    void Scope::makeAssign(const parser::Assign *node) {
        auto context = ops::Context::from(*this);

        auto destination = ops::expression::makeExpression(context, node->children.front()->as<parser::Expression>());

        auto sourceRaw = ops::expression::makeExpression(context, node->children.back()->as<parser::Expression>());
        auto sourceConverted = ops::makeConvert(context, sourceRaw, destination.type);

        if (!sourceConverted) {
            throw VerifyError(node, "Assignment of type {} to {} is not allowed.", toString(sourceRaw.type),
                toString(destination.type));
        }

        auto source = std::move(*sourceConverted);

        if (!destination.isSet(builder::Result::FlagReference) || !destination.isSet(builder::Result::FlagMutable)) {
            throw VerifyError(node, "Left side of assign expression must be a mutable variable.");
        }

        if (current) {
            llvm::Value *result;

            try {
                if (node->op == parser::Assign::Operator::Assign) {
                    result = ops::get(context, ops::makePass(context, source));
                } else {
                    auto operation = ([&]() {
                        switch (node->op) {
                        case parser::Assign::Operator::Plus:
                            return ops::binary::makeAdd(context, destination, source);
                        case parser::Assign::Operator::Minus:
                            return ops::binary::makeSub(context, destination, source);
                        case parser::Assign::Operator::Multiply:
                            return ops::binary::makeMul(context, destination, source);
                        case parser::Assign::Operator::Divide:
                            return ops::binary::makeDiv(context, destination, source);
                        case parser::Assign::Operator::Modulo:
                            return ops::binary::makeMod(context, destination, source);
                        default:
                            throw std::runtime_error("Unimplemented assign node operator.");
                        }
                    })();

                    result = ops::get(context, ops::makePass(context, operation));
                }
            } catch (const std::runtime_error &e) { throw VerifyError(node, "{}", e.what()); }

            current->CreateStore(result, destination.value);
        }
    }
}
