#pragma once

#include <parser/kinds.h>

struct CodeNode : public Node {
    explicit CodeNode(Node *parent);
};
