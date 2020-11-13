#include <parser/root.h>

#include <parser/function.h>

RootNode::RootNode(State &state) : Node(state, Kind::Root) {
    while (!end()) {
        push<FunctionNode>();
    }
}
