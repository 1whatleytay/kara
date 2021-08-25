#include <parser/assign.h>

#include <parser/expression.h>

namespace kara::parser {
    const Expression *Assign::left() const { return children[0]->as<Expression>(); }

    const Expression *Assign::right() const { return children[1]->as<Expression>(); }

    Assign::Assign(Node *parent)
        : Node(parent, Kind::Assign) {
        push<Expression>();

        op = select<Operator>({
            { "=", Operator::Assign },
            { "+=", Operator::Plus },
            { "-=", Operator::Minus },
            { "*=", Operator::Multiply },
            { "/=", Operator::Divide },
            { "%=", Operator::Modulo },
        });
        match();

        push<Expression>();
    }
}
