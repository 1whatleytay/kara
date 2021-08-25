#include <parser/import.h>

#include <parser/literals.h>

namespace kara::parser {
    const String *Import::body() const { return children.front()->as<String>(); }

    Import::Import(Node *parent)
        : Node(parent, Kind::Import) {
        match("import", true);

        if (next("(")) {
            type = token();

            needs(")");
        }

        push<String>();

        assert(children.back()->as<String>()->inserts.empty());
    }
}
