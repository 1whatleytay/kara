#include <parser/type.h>

#include <parser/variable.h>

const Node *TypeNode::alias() const {
    return isAlias ? children.front().get() : nullptr;
}

std::vector<const VariableNode *> TypeNode::fields() const {
    std::vector<const VariableNode *> result(children.size());

    for (size_t a = 0; a < children.size(); a++)
        result[a] = children[a]->as<VariableNode>();

    return result;
}

TypeNode::TypeNode(Node *parent, bool external) : Node(parent, Kind::Type) {
    if (external)
        return;

    match("type", true);

    name = token();

    enum class Operators {
        NewClass,
        Alias
    };

    auto check = select<Operators>({ "{", "=" });

    switch (check) {
        case Operators::NewClass:
            while (!end() && !peek("}")) {
                push<VariableNode>(false, false);

                next(",");
            }

            needs("}");

            break;

        case Operators::Alias:
            isAlias = true;
            pushTypename(this);

            break;

        default:
            throw;
    }
}
