#include <parser/operator.h>

OperatorNode::OperatorNode(Node *parent) : Node(parent, Kind::Operator) {
    std::vector<std::string> doNotCapture = { "+=", "-=", "*=", "/=" };

    if (select(doNotCapture, false, true) != doNotCapture.size())
        error("Operator cannot capture this text.");

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
