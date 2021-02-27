#include <parser/number.h>

#include <fmt/format.h>

#include <algorithm>

NumberNode::NumberNode(Node *parent) : Node(parent, Kind::Number) {
    bool positive = select({ "-", "+" }, false, true) != 0;

    auto temp = tokenStoppable;
    tokenStoppable = [&](const char *c, size_t s) {
        return *c != '.' && anyHard(c, s); // capture dot too
    };
    std::string full = token();
    tokenStoppable = temp;

    full.erase(std::remove_if(full.begin(), full.end(), [](char c) { return c == '_'; }), full.end());

    // Just a quick check.
    if (std::any_of(full.begin(), full.end(), [](char a) { return a != '.' && !std::isdigit(a); }))
        error(fmt::format("Token {} is not a number.", full));

    try {
        if (full.find('.') == std::string::npos) {
            uint64_t v = std::stoull(full);

            if (v > INT64_MAX) {
                if (!positive)
                    error(fmt::format("Token {} cannot be represented by an integer.", full));

                type = types::u64();
                value.u = v;
            } else {
                type = types::i64();
                value.i = static_cast<int64_t>(v) * (positive ? +1 : -1);
            }
        } else {
            double v = std::stod(full);

            type = types::f64();
            value.f = v * (positive ? +1.0 : -1.0);
        }
    } catch (const std::out_of_range &e) {
        error(fmt::format("Token {} cannot be represented by a number.", full));
    } catch (const std::invalid_argument &e) {
        error(fmt::format("Token {} is not a number..", full));
    }
}
