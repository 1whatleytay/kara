#pragma once

#include <parser/kinds.h>

struct AssignNode : public Node {
    explicit AssignNode(Node *parent);
};
