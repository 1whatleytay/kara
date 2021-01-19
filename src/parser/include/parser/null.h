#pragma once

#include <parser/kinds.h>

struct NullNode : public Node {
    explicit NullNode(Node *parent);
};
