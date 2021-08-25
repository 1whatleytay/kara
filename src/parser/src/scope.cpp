#include <parser/scope.h>

#include <parser/assign.h>
#include <parser/expression.h>
#include <parser/statement.h>
#include <parser/variable.h>

namespace kara::parser {
    Code::Code(Node *parent)
        : Node(parent, Kind::Code) {
        while (!end() && !peek("}")) {
            push<Block, Insight, If, For, Statement, Assign, Variable, Expression>();

            while (next(","))
                ;
        }
    }

    const Code *Block::body() const { return children.front()->as<Code>(); }

    Block::Block(Node *parent)
        : Node(parent, Kind::Block) {
        type = select<Type>({ { "block", Type::Regular }, { "exit", Type::Exit } }, true);
        match();

        needs("{");

        push<Code>();

        needs("}");
    }

    const Expression *If::condition() const { return children.front()->as<Expression>(); }

    const Code *If::onTrue() const { return children[1]->as<Code>(); }

    const hermes::Node *If::onFalse() const { return children.size() >= 2 ? children[2].get() : nullptr; }

    If::If(Node *parent)
        : Node(parent, Kind::If) {
        match("if", true);

        push<Expression>();

        needs("{");

        push<Code>();

        needs("}");

        if (next("else", true)) {
            if (next("{")) {
                push<Code>();

                needs("}");
            } else {
                push<If>(); // recursive :D
            }
        }
    }

    const Variable *ForIn::name() const { return children[0]->as<Variable>(); }

    const Expression *ForIn::expression() const { return children[1]->as<Expression>(); }

    ForIn::ForIn(Node *parent)
        : Node(parent, Kind::ForIn) {
        push<Variable>(false);

        match("in", true);

        push<Expression>();
    }

    const hermes::Node *For::condition() const { return infinite ? nullptr : children[0].get(); }

    const Code *For::body() const { return children[!infinite]->as<Code>(); }

    For::For(Node *parent)
        : Node(parent, Kind::For) {
        match("for", true);

        if (!next("{")) {
            infinite = false;

            push<ForIn, Expression>();

            needs("{");
        }

        push<Code>();

        needs("}");
    }
}
