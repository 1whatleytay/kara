#include <parser/operator.h>

OperatorNode::OperatorNode(Node *parent) : Node(parent, Kind::Operator) {
    op = select<Operation>({
        "+", // Add
        "-", // Sub
        "*", // Mul
        "/", // Div
        "==", // Equals
        "!=", // NotEquals
        ">", // Greater
        ">=", // GreaterEqual
        "<", // Lesser
        "<=", // LesserEqual
        "&&", // And
        "||" // Or
    });
}
