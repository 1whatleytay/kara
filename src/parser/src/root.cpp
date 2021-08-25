#include <parser/root.h>

#include <parser/function.h>
#include <parser/import.h>
#include <parser/type.h>
#include <parser/variable.h>

namespace kara::parser {
    Root::Root(hermes::State &state, bool external)
        : Node(state, Kind::Root) {
        if (external)
            return;

        spaceStoppable = [&state, this](const char *text, size_t size) {
            if (size >= 2) {
                if (memcmp(text, "//", 2) == 0) {
                    state.index += 2;

                    size_t toGo = state.until([](const char *t, size_t) { return *t == '\n'; });
                    state.pop(toGo + 1, spaceStoppable);

                    return true;
                } else if (memcmp(text, "/*", 2) == 0) {
                    state.index += 2;

                    size_t toGo
                        = state.until([](const char *t, size_t s) { return s >= 2 && memcmp(t, "*/", 2) == 0; });
                    state.pop(toGo + 2, spaceStoppable);

                    return true;
                }
            }

            return notSpace(text, size);
        };

        state.push(spaceStoppable); // needed to start parsing properly

        while (!end()) {
            push<Import, Type, Variable, Function>();

            while (next(","))
                ;
        }
    }
}
