#include <parser/literals.h>

#include <parser/expression.h>
#include <parser/typename.h>

#include <fmt/format.h>

#include <cassert>
#include <sstream>
#include <algorithm>

namespace kara::parser {
    const Expression *Parentheses::body() const { return children.front()->as<Expression>(); }

    Parentheses::Parentheses(Node *parent)
        : Node(parent, Kind::Parentheses) {
        if (next("group"))
            match();

        match("(");

        push<Expression>();

        needs(")");
    }

    Bool::Bool(Node *parent)
        : Node(parent, Kind::Bool) {
        value = select<bool>({ { "false", false }, { "true", true } }, true);
    }

    Special::Special(Node *parent)
        : Node(parent, Kind::Special) {
        type = select<utils::SpecialType>(
            {
                { "any", utils::SpecialType::Any },
                { "nothing", utils::SpecialType::Nothing },
                { "null", utils::SpecialType::Null },
            },
            true);
    }

    Number::Number(Node *parent, bool external)
        : Node(parent, Kind::Number) {
        if (external)
            return;

        std::optional<int64_t> sign = maybe<int64_t>({ { "-", -1 }, { "+", +1 } }, false);
        bool positive = !sign || *sign > 0;
        bool explicitPositive = sign && positive;

        size_t start = state.index;

        auto temp = tokenStoppable;
        bool hasCapturedDot = false; // only capture one dot
        tokenStoppable = [&](const char *c, size_t s) {
            if (*c == '.') {
                if (hasCapturedDot)
                    return true;
                else
                    hasCapturedDot = true;
            }

            return *c != '.' && temp(c, s); // capture dot too
        };
        std::string full = token();
        tokenStoppable = temp;

        full.erase(std::remove_if(full.begin(), full.end(), [](char c) { return c == '_'; }), full.end());

        auto pos = static_cast<int64_t>(full.rfind('.'));
        assert(full.rfind('.') == full.find('.'));

        auto digit = [](char c) { return std::isdigit(c); };

        if (pos != std::string::npos && !std::all_of(full.begin() + pos + 1, full.end(), digit)) {
            state.index = start + pos; // rollback
            full = full.substr(0, pos);
        }

        // Just a quick check.
        if (std::any_of(full.begin(), full.end(), [](char a) { return a != '.' && !std::isdigit(a); }))
            error(fmt::format("Token {} is not a number.", full));

        try {
            if (full.find('.') == std::string::npos) {
                uint64_t v = std::stoull(full);

                if (v > INT64_MAX || explicitPositive) {
                    value = v;
                } else {
                    value = (positive ? +1 : -1) * static_cast<int64_t>(v);
                }
            } else {
                double v = std::stod(full);

                value = v * (positive ? +1.0 : -1.0);
            }
        } catch (const std::out_of_range &e) {
            error(fmt::format("Token {} cannot be represented by a number.", full));
        } catch (const std::invalid_argument &e) { error(fmt::format("Token {} is not a number.", full)); }
    }

    String::String(Node *parent)
        : Node(parent, Kind::String) {
        auto defaultPop = spaceStoppable;

        // do not skip text
        auto alwaysStop = [](const char *, size_t) { return true; };
        spaceStoppable = alwaysStop;

        std::stringstream stream;

        auto quote = select<std::string>({ { "\'", "\'" }, { "\"", "\"" } });
        match();

        enum class BreakChars {
            Dollar,
            Backslash,
            Quote,
        };

        enum class SpecialChars {
            NewLine,
            Tab,
            DollarSign,
            Null,
            Carriage,
            SingleQuote,
            DoubleQuote,
            Backslash,
        };

        std::vector<std::string> breakChars = { "$", "\\", quote };
        hermes::SelectMap<BreakChars> breakCharsMap = {
            { "$", BreakChars::Dollar },
            { "\\", BreakChars::Backslash },
            { quote, BreakChars::Quote },
        };

        hermes::SelectMap<SpecialChars> specialCharsMap = {
            { "n", SpecialChars::NewLine },
            { "r", SpecialChars::Carriage },
            { "t", SpecialChars::Tab },
            { "$", SpecialChars::DollarSign },
            { "0", SpecialChars::Null },
            { "\'", SpecialChars::Backslash },
            { "\"", SpecialChars::SingleQuote },
            { "\\", SpecialChars::DoubleQuote },
        };

        bool loop = true;
        while (loop) {
            stream << until(breakChars);

            if (peek(quote))
                spaceStoppable = defaultPop;

            switch (select(breakCharsMap)) {
            case BreakChars::Dollar:
                spaceStoppable = notSpace;
                needs("{");

                inserts.push_back(stream.str().size());
                push<Expression>();

                spaceStoppable = alwaysStop;
                needs("}");

                break;

            case BreakChars::Backslash:
                switch (select(specialCharsMap)) {
                case SpecialChars::NewLine:
                    stream << '\n';
                    break;
                case SpecialChars::Tab:
                    stream << '\t';
                    break;
                case SpecialChars::DollarSign:
                    stream << '$';
                    break;
                case SpecialChars::Null:
                    stream << '\0';
                    break;
                case SpecialChars::Carriage:
                    stream << '\r';
                    break;
                case SpecialChars::SingleQuote:
                    stream << '\'';
                    break;
                case SpecialChars::DoubleQuote:
                    stream << '\"';
                    break;
                case SpecialChars::Backslash:
                    stream << '\\';
                    break;
                }

                break;

            case BreakChars::Quote:
                loop = false;
                break;
            }
        }

        text = stream.str();
    }

    std::vector<const Expression *> Array::elements() const {
        std::vector<const Expression *> result(children.size());

        for (size_t a = 0; a < children.size(); a++)
            result[a] = children[a]->as<Expression>();

        return result;
    }

    Array::Array(Node *parent)
        : Node(parent, Kind::Array) {
        match("[");

        while (!end() && !peek("]")) {
            push<Expression>();

            next(","); // optional ig
        }

        needs("]");
    }

    Reference::Reference(Node *parent)
        : Node(parent, Kind::Reference) {
        name = token();
    }

    const hermes::Node *New::type() const { return children.front().get(); }

    New::New(Node *parent)
        : Node(parent, Kind::New) {
        match("*");

        pushTypename(this);
    }
}
