#include <parser/statement.h>

#include <parser/expression.h>

namespace kara::parser {
    const Expression *Insight::expression() { return children.front()->as<Expression>(); }

    Insight::Insight(Node *parent)
        : Node(parent, Kind::Insight) {
        match("insight", true);

        push<Expression>();
    }

    Statement::Statement(Node *parent)
        : Node(parent, Kind::Statement) {
        op = select<Operation>(
            {
                { "return", Operation::Return },
                { "break", Operation::Break },
                { "continue", Operation::Continue },
            },
            true);

        match();

        if (op == Operation::Return && !peek("}")) {
            push<Expression>();

            if (!peek("}"))
                error("Must have closing bracket after return statement.");
        }
    }
}
