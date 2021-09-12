#include <builder/builder.h>

#include <builder/error.h>
#include <builder/operations.h>

#include <parser/expression.h>
#include <parser/statement.h>

namespace kara::builder {
    void Scope::makeStatement(const parser::Statement *node) {
        assert(current);

        auto context = ops::Context::from(*this);

        auto nothing = from(utils::PrimitiveType::Nothing);

        switch (node->op) {
        case parser::Statement::Operation::Return: {
            assert(function);

            if (node->children.empty()) {
                if (*function->type.returnType != nothing) {
                    throw VerifyError(node,
                        "Method is of type {} but return statement does not "
                        "return anything",
                        toString(*function->type.returnType));
                }
            } else {
                if (!node->children.empty() && *function->type.returnType == nothing) {
                    throw VerifyError(node,
                        "Method does not have a return type but return "
                        "statement returns value.");
                }

                // lambda :S

                auto expressionNode = node->children.front()->as<parser::Expression>();

                auto resultRaw = ops::expression::make(context, expressionNode);
                auto resultConverted = ops::makeConvert(context, resultRaw, *function->type.returnType);

                if (!resultConverted.has_value()) {
                    throw VerifyError(node, "Cannot return {} from a function that returns {}.",
                        toString(resultRaw.type), toString(*function->type.returnType));
                }

                builder::Result result = ops::makePass(context, *resultConverted);

                current->CreateStore(ops::get(context, result), function->returnValue);
            }

            // might want to make ops::makeAccumulatorCommit(const Context &, const builder::Accumulator &)
            if (context.ir)
                accumulator.commit(context.builder, *context.ir);

            // TODO: something needs to be done about this insert block... this is a temp solution
            exit(ExitPoint::Return, context.ir ? context.ir->GetInsertBlock() : nullptr);

            break;
        }

        case parser::Statement::Operation::Break:
            exit(ExitPoint::Break);
            break;

        case parser::Statement::Operation::Continue:
            exit(ExitPoint::Continue);
            break;

        default:
            throw;
        }
    }
}
