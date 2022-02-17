#include <parser/operator.h>

#include <parser/expression.h>
#include <parser/literals.h>
#include <parser/typename.h>

namespace kara::parser {
    const hermes::Node *As::type() const { return children.front().get(); }

    As::As(Node *parent)
        : Node(parent, Kind::As) {
        match("as");

        pushTypename(this);
    }

    CallParameterName::CallParameterName(Node *parent)
        : Node(parent, Kind::CallParameterName) {
        name = token();

        match(":");
    }

    std::vector<const Expression *> Call::parameters() const {
        std::vector<const Expression *> result;

        for (const auto &c : children) {
            if (c->is(Kind::Expression)) {
                result.push_back(c->as<Expression>());
            }
        }

        return result;
    }

    std::unordered_map<size_t, const CallParameterName *> Call::names() const {
        std::unordered_map<size_t, const CallParameterName *> result;

        size_t index = 0;

        for (const auto &c : children) {
            if (c->is(Kind::Expression))
                index++;

            if (c->is(Kind::CallParameterName))
                result[index] = c->as<CallParameterName>();
        }

        return result;
    }

    std::unordered_map<size_t, std::string> Call::namesStripped() const {
        std::unordered_map<size_t, std::string> result;

        auto v = names();

        for (const auto &c : v)
            result[c.first] = c.second->name;

        return result;
    }

    Call::Call(Node *parent)
        : Node(parent, Kind::Call) {
        match("(");

        bool first = true;
        while (!end() && !peek(")")) {
            if (!first)
                needs(",");
            else
                first = false;

            push<CallParameterName>(true);
            push<Expression>();
        }

        needs(")");
    }

    const Unary *Dot::unary() const {
        return children.front()->is(Kind::Unary) ? children.front()->as<Unary>() : nullptr;
    }

    const Reference *Dot::reference() const {
        return children.front()->is(Kind::Reference) ? children.front()->as<Reference>() : nullptr;
    }

    Dot::Dot(Node *parent)
        : Node(parent, Kind::Dot) {
        match(".");

        push<Unary, Reference>();
    }

    const Expression *Index::index() const { return children.front()->as<Expression>(); }

    Index::Index(Node *parent)
        : Node(parent, Kind::Index) {
        match("[");

        push<Expression>();

        needs("]");
    }

    const Expression *Ternary::onTrue() const { return children[0]->as<Expression>(); }

    const Expression *Ternary::onFalse() const { return children[1]->as<Expression>(); }

    Ternary::Ternary(Node *parent)
        : Node(parent, Kind::Ternary) {
        match("?");

        push<Expression>();

        needs(":");

        push<Expression>();
    }

    Slash::Slash(Node *parent)
        : Node(parent, Kind::Slash) {
        match("\\");
    }

    Unary::Unary(Node *parent)
        : Node(parent, Kind::Unary) {
        auto temp = maybe<utils::UnaryOperation>(
            {
                { "!", utils::UnaryOperation::Not },
                { "-", utils::UnaryOperation::Negative },
                { "&", utils::UnaryOperation::Reference },
                { "@", utils::UnaryOperation::Fetch },
            }, false);

        if (temp) {
            op = *temp;
            return;
        }

        temp = maybe<utils::UnaryOperation>(
            {
                { "move", utils::UnaryOperation::Move },
            }, true);

        if (temp) {
            op = *temp;
            return;
        }

        error("Expected unary operation.");
    }

    Operator::Operator(Node *parent)
        : Node(parent, Kind::Operator) {
        // Do not capture...
        if (maybe<bool>(
                {
                    { "+=", true },
                    { "-=", true },
                    { "*=", true },
                    { "/=", true },
                    { "%=", true },
                },
                false)) {
            error("Operator cannot capture this text.");
        }

        op = select<utils::BinaryOperation>({
            { "+", utils::BinaryOperation::Add },
            { "-", utils::BinaryOperation::Sub },
            { "*", utils::BinaryOperation::Mul },
            { "/", utils::BinaryOperation::Div },
            { "%", utils::BinaryOperation::Mod },
            { "==", utils::BinaryOperation::Equals },
            { "!=", utils::BinaryOperation::NotEquals },
            { ">=", utils::BinaryOperation::GreaterEqual },
            { "<=", utils::BinaryOperation::LesserEqual },
            { ">", utils::BinaryOperation::Greater },
            { "<", utils::BinaryOperation::Lesser },
            { "&&", utils::BinaryOperation::And },
            { "||", utils::BinaryOperation::Or },
            { "??", utils::BinaryOperation::Fallback },
        });
    }
}
