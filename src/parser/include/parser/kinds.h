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
    Insight,
    Code,
    Reference,
    Number,
    Parentheses,
    Block,
    Unary,
    Call,
    CallParameterName,
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
