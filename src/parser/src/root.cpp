#include <parser/root.h>

#include <parser/type.h>
#include <parser/import.h>
#include <parser/function.h>

RootNode::RootNode(State &state, bool external) : Node(state, Kind::Root) {
    if (external)
        return;

    while (!end()) {
        push<ImportNode, TypeNode, FunctionNode>();
    }
}
