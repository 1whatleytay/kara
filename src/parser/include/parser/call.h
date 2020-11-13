#pragma once

#include <parser/kinds.h>

struct CallNode : public Node {
    explicit CallNode(Node *parent);
};
