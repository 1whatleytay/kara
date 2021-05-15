#include <parser/root.h>

#include <parser/type.h>
#include <parser/import.h>
#include <parser/function.h>
#include <parser/variable.h>

RootNode::RootNode(State &state, bool external) : Node(state, Kind::Root) {
    if (external)
        return;

    spaceStoppable = [this](const char *text, size_t size) {
        if (size >= 2) {
            if (memcmp(text, "//", 2) == 0) {
                this->state.index += 2;

                size_t toGo = this->state.until([](const char *t, size_t) {
                    return *t == '\n';
                });
                this->state.pop(toGo + 1, this->spaceStoppable);

                return true;
            } else if (memcmp(text, "/*", 2) == 0) {
                this->state.index += 2;

                size_t toGo = this->state.until([](const char *t, size_t s) {
                    return s >= 2 && memcmp(t, "*/", 2) == 0;
                });
                this->state.pop(toGo + 2, this->spaceStoppable);

                return true;
            }
        }

        return notSpace(text, size);
    };

    while (!end()) {
        push<ImportNode, TypeNode, VariableNode, FunctionNode>();

        while (next(","));
    }
}
