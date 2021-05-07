#include <parser/string.h>

#include <parser/expression.h>

#include <sstream>

StringNode::StringNode(Node *parent) : Node(parent, Kind::String) {
    auto defaultPop = spaceStoppable;

    // do not skip text
    auto alwaysStop = [](const char *, size_t) { return true; };
    spaceStoppable = alwaysStop;

    std::stringstream stream;

    std::vector<std::string> quotes = { "\'", "\"" };

    std::string quote = quotes[select(quotes)];
    match();

    enum class BreakChars {
        Dollar,
        Backslash,
        Quote,
    };
    std::vector<std::string> breakChars = { "$", "\\", quote };

    bool loop = true;
    while (loop) {
        stream << until(breakChars);

        if (peek("'"))
            spaceStoppable = defaultPop;

        switch (select<BreakChars>(breakChars)) {
            case BreakChars::Dollar:
                spaceStoppable = notSpace;
                needs("{");

                inserts.push_back(stream.str().size());
                push<ExpressionNode>();

                spaceStoppable = alwaysStop;
                needs("}");

                break;

            case BreakChars::Backslash:
                enum class SpecialChars {
                    NewLine,
                    Tab,
                    DollarSign,
                    SingleQuote,
                    DoubleQuote,
                    Backslash,
                };

                switch (select<SpecialChars>({ "n", "t", "$", "\'", "\"", "\\" })) {
                    case SpecialChars::NewLine:
                        stream << '\n';
                        break;
                    case SpecialChars::Tab:
                        stream << '\t';
                        break;
                    case SpecialChars::DollarSign:
                        stream << '$';
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
