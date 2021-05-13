#pragma once

#include <hermes/node.h>

using namespace hermes;

enum class Kind {
	Root,
    Function,
    Variable,
    NamedTypename,
    PrimitiveTypename,
    OptionalTypename,
    ReferenceTypename,
    ArrayTypename,
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
    If,
    For,
    ForIn,
    Bool,
    Special,
    Array,
    Index,
    Type,
    Dot,
    Ternary,
    As,
    Import,
    String,
};
