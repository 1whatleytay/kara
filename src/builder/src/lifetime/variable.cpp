#include <builder/lifetime/variable.h>

#include <builder/builder.h>

#include <parser/variable.h>

#include <fmt/format.h>

std::string VariableLifetime::toString() const {
    return fmt::format("{}{}", placeholderString(), node ? node->name : "<anon>");
}

std::shared_ptr<Lifetime> VariableLifetime::copy() const {
    return std::make_shared<VariableLifetime>(node, id);
}

bool VariableLifetime::resolves(const BuilderScope &scope) const {
    // Is a placeholder node, definitely outlives the function... I think.
    if (!node)
        return true;

    return scope.findVariable(node).has_value();
}

bool VariableLifetime::operator==(const Lifetime &lifetime) const {
    if (!Lifetime::operator==(lifetime))
        return false;

    auto varLifetime = dynamic_cast<const VariableLifetime &>(lifetime);

    return node == varLifetime.node;
}

VariableLifetime::VariableLifetime(const VariableNode *node, PlaceholderId id)
    : Lifetime(Lifetime::Kind::Variable, std::move(id)), node(node) { }
