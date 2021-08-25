#include <builder/builder.h>

#include <builder/error.h>

#include <parser/expression.h>
#include <parser/statement.h>

namespace kara::builder {
    void Scope::makeStatement(const parser::Statement *node) {
        assert(current);

        auto nothing = utils::PrimitiveTypename::from(utils::PrimitiveType::Nothing);

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

                builder::Result resultRaw = makeExpression(node->children.front()->as<parser::Expression>());
                std::optional<builder::Result> resultConverted = convert(resultRaw, *function->type.returnType);

                if (!resultConverted.has_value()) {
                    throw VerifyError(node, "Cannot return {} from a function that returns {}.",
                        toString(resultRaw.type), toString(*function->type.returnType));
                }

                builder::Result result = pass(*resultConverted);

                current->CreateStore(get(result), function->returnValue);
            }

            statementContext.commit(currentBlock);

            exit(ExitPoint::Return);

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
