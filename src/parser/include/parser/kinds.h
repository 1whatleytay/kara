#pragma once

#include <hermes/node.h>

using namespace hermes;

enum class Kind {
	Root,
    Function,
    Variable,
    Typename,
    Assign,
    Expression,
    Operator,
    Statement,
    Code,
    Reference,
    Number,
    Parentheses,
    Block,
    Unary,
    Call,
    Debug,
    If,
    For,
    ForIn,
    Bool,
    Null,
    Array,
    Index,
    Type,
    Dot,
    Ternary,
};
