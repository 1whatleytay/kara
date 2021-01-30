#pragma once

#include <parser/kinds.h>

struct IndexNode : public Node {
    explicit IndexNode(Node *parent);
};
