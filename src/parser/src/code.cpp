#include <parser/scope.h>

#include <parser/if.h>
#include <parser/for.h>
#include <parser/block.h>
#include <parser/debug.h>
#include <parser/assign.h>
#include <parser/variable.h>
#include <parser/statement.h>
#include <parser/expression.h>

CodeNode::CodeNode(Node *parent) : Node(parent, Kind::Code) {
    while (!end() && !peek("}")) {
        push<DebugNode, BlockNode, IfNode, ForNode, StatementNode, AssignNode, VariableNode, ExpressionNode>();
    }
}
