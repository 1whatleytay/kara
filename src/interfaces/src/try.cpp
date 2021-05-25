#include <interfaces/interfaces.h>

#include <parser/type.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/variable.h>

#include <fmt/printf.h>

std::string toTypeString(const Node *node) {
    switch (node->is<Kind>()) {
        case Kind::NamedTypename:
            return node->as<NamedTypenameNode>()->name;
        case Kind::PrimitiveTypename:
            switch (node->as<PrimitiveTypenameNode>()->type) {
                case PrimitiveType::Any: return "any";
                case PrimitiveType::Null: return "null";
                case PrimitiveType::Nothing: return "nothing";
                case PrimitiveType::Bool: return "bool";
                case PrimitiveType::Byte: return "byte";
                case PrimitiveType::Short: return "short";
                case PrimitiveType::Int: return "int";
                case PrimitiveType::Long: return "long";
                case PrimitiveType::UByte: return "ubyte";
                case PrimitiveType::UShort: return "ushort";
                case PrimitiveType::UInt: return "uint";
                case PrimitiveType::ULong: return "ulong";
                case PrimitiveType::Float: return "float";
                case PrimitiveType::Double: return "double";
                default:
                    assert(false);
            }
        case Kind::OptionalTypename: {
            auto e = node->as<OptionalTypenameNode>();

            return fmt::format("{}{}", e->bubbles ? "!" : "?", toTypeString(e->body()));
        }

        case Kind::ReferenceTypename: {
            auto e = node->as<ReferenceTypenameNode>();

            return fmt::format("&{}{}", e->isMutable ? "var " : "", toTypeString(e->body()));
        }
        case Kind::ArrayTypename: {
            auto e = node->as<ArrayTypenameNode>();

            switch (e->type) {
                case ArrayKind::VariableSize:
                    return fmt::format("[{}]", toTypeString(e->body()));
                case ArrayKind::FixedSize:
                    return fmt::format("[{}:{}]", toTypeString(e->body()), std::get<uint64_t>(e->fixedSize()->value));
                case ArrayKind::Unbounded:
                    return fmt::format("[{}:]", toTypeString(e->body()));
                case ArrayKind::Iterable:
                    return fmt::format("[{}::]", toTypeString(e->body()));
            }
        }

        default:
            assert(false);
    }
}

bool verify(const Node *node) {
    for (const auto &c : node->children) {
        if (c->parent != node)
            return false;

        verify(c.get());
    }

    return true;
}

int main(int count, const char **args) {
    auto [state, root] = interfaces::header::create(count, args);

    assert(verify(root.get()));

    for (const auto &e : root->children) {
        switch (e->is<Kind>()) {
            case Kind::Function: {
                auto f = e->as<FunctionNode>();

                std::vector<std::string> text;
                text.reserve(f->parameterCount);

                for (size_t a = 0; a < f->parameterCount; a++) {
                    auto *v = f->children[a]->as<VariableNode>();

                    assert(v->hasFixedType);

                    text.push_back(fmt::format("{} {}", v->name, toTypeString(v->fixedType())));
                }

                fmt::print("{}({}) {} external\n", f->name, fmt::join(text, ", "), toTypeString(f->fixedType()));

                break;
            }

            case Kind::Variable: {
                auto v = e->as<VariableNode>();

                auto toString = [](const NumberNode *node) -> std::string {
                    return std::visit([](auto x) { return std::to_string(x); }, node->value);
                };

                assert(!v->hasInitialValue);

                fmt::print("{} {} {}{}{}\n",
                    v->isMutable ? "var" : "let", v->name,
                    toTypeString(v->fixedType()), v->isExternal ? " external" : "",
                    v->hasConstantValue ? fmt::format(" = {}", toString(v->constantValue())) : "");

                break;
            }

            case Kind::Type: {
                auto t = e->as<TypeNode>();

                if (t->isAlias) {
                    fmt::print("type {} = {}\n", t->name, toTypeString(t->alias()));
                } else {
                    std::vector<std::string> elements;

                    auto fields = t->fields();
                    elements.reserve(fields.size());

                    for (auto field : fields) {
                        elements.push_back(fmt::format("{}{} {}",
                            field->isMutable ? "var " : "", field->name, toTypeString(field->fixedType())));
                    }

                    fmt::print("type {} {{\n\t{}\n}}\n", t->name, fmt::join(elements, "\n\t"));
                }

                break;
            }

            default:
                assert(false);
        }
    }
}
