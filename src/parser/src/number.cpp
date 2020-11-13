#include <parser/number.h>

#include <fmt/format.h>

#include <algorithm>

static bool isNumber(const std::string &text) {
    return !std::any_of(text.begin(), text.end(), [](char a) { return !std::isdigit(a); });
}

NumberNode::NumberNode(Node *parent) : Node(parent, Kind::Number) {
    std::string top = token();

    if (!isNumber(top))
        error("Not a number node.");

    value = std::stoull(top);
}
