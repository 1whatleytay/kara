#pragma once

#include <parser/kinds.h>

struct ParenthesesNode : public Node {
    explicit ParenthesesNode(Node *parent);
};
