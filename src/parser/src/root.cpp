#include <parser/root.h>

#include <parser/type.h>
#include <parser/function.h>

RootNode::RootNode(State &state) : Node(state, Kind::Root) {
    while (!end()) {
        push<TypeNode, FunctionNode>();
    }
}
