#pragma once

#include <parser/kinds.h>

struct ArrayNode : public Node {
    explicit ArrayNode(Node *parent);
};
