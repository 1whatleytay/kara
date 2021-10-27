#include <interfaces/interfaces.h>

#include <parser/function.h>
#include <parser/literals.h>
#include <parser/type.h>
#include <parser/variable.h>

#include <utils/expression.h>

#include <fmt/printf.h>

using namespace kara;

std::string toTypeString(const hermes::Node *node) {
    switch (node->is<parser::Kind>()) {
    case parser::Kind::NamedTypename:
        return node->as<parser::NamedTypename>()->name;
    case parser::Kind::PrimitiveTypename:
        switch (node->as<parser::PrimitiveTypename>()->type) {
        case utils::PrimitiveType::Any:
            return "any";
        case utils::PrimitiveType::Null:
            return "null";
        case utils::PrimitiveType::Nothing:
            return "nothing";
        case utils::PrimitiveType::Bool:
            return "bool";
        case utils::PrimitiveType::Byte:
            return "byte";
        case utils::PrimitiveType::Short:
            return "short";
        case utils::PrimitiveType::Int:
            return "int";
        case utils::PrimitiveType::Long:
            return "long";
        case utils::PrimitiveType::UByte:
            return "ubyte";
        case utils::PrimitiveType::UShort:
            return "ushort";
        case utils::PrimitiveType::UInt:
            return "uint";
        case utils::PrimitiveType::ULong:
            return "ulong";
        case utils::PrimitiveType::Float:
            return "float";
        case utils::PrimitiveType::Double:
            return "double";
        default:
            throw;
        }
    case parser::Kind::OptionalTypename: {
        auto e = node->as<parser::OptionalTypename>();

        return fmt::format("{}{}", e->bubbles ? "!" : "?", toTypeString(e->body()));
    }

    case parser::Kind::ReferenceTypename: {
        auto e = node->as<parser::ReferenceTypename>();

        return fmt::format("&{}{}", e->isMutable ? "var " : "", toTypeString(e->body()));
    }

    case parser::Kind::ArrayTypename: {
        auto e = node->as<parser::ArrayTypename>();

        switch (e->type) {
        case utils::ArrayKind::VariableSize:
            return fmt::format("[{}]", toTypeString(e->body()));
        case utils::ArrayKind::FixedSize:
            return fmt::format("[{}:{}]", toTypeString(e->body()), std::get<uint64_t>(e->fixedSize()->value));
        case utils::ArrayKind::Unbounded:
            return fmt::format("[{}:]", toTypeString(e->body()));
        case utils::ArrayKind::Iterable:
            return fmt::format("[{}::]", toTypeString(e->body()));
        default:
            throw;
        }
    }

    case parser::Kind::FunctionTypename: {
        auto e = node->as<parser::FunctionTypename>();

        auto parameters = e->parameters();

        std::vector<std::string> text(parameters.size());
        std::transform(parameters.begin(), parameters.end(), text.begin(), [](const auto &p) {
            return toTypeString(p);
        });

        auto locked = e->isLocked ? " locked" : "";
        const char *flags = "";

        switch (e->kind) {
        case utils::FunctionKind::Pointer:
            flags = " ptr";
        case utils::FunctionKind::Regular:
            break;
        default:
            throw;
        }

        return fmt::format("func{}{}({}) {}", locked, flags, fmt::join(text, ", "), toTypeString(e->returnType()));
    }

    default:
        throw;
    }
}

bool verify(const hermes::Node *node) {
    for (const auto &c : node->children) {
        if (c->parent != node)
            return false;

        verify(c.get());
    }

    return true;
}

int main(int count, const char **args) {
    auto [state, root] = kara::interfaces::header::create(count, args);

    assert(verify(root.get()));

    for (const auto &e : root->children) {
        switch (e->is<parser::Kind>()) {
        case parser::Kind::Function: {
            auto f = e->as<parser::Function>();

            std::vector<std::string> text;
            text.reserve(f->parameterCount);

            for (size_t a = 0; a < f->parameterCount; a++) {
                auto *v = f->children[a]->as<parser::Variable>();

                assert(v->hasFixedType);

                text.push_back(fmt::format("{} {}", v->name, toTypeString(v->fixedType())));
            }

            fmt::print("{}({}) {} external\n", f->name, fmt::join(text, ", "), toTypeString(f->fixedType()));

            break;
        }

        case parser::Kind::Variable: {
            auto v = e->as<parser::Variable>();

            auto toString = [](const parser::Number *node) -> std::string {
                return std::visit([](auto x) { return std::to_string(x); }, node->value);
            };

            assert(!v->hasInitialValue);

            fmt::print("{} {} {}{}{}\n", v->isMutable ? "var" : "let", v->name, toTypeString(v->fixedType()),
                v->isExternal ? " external" : "",
                v->hasConstantValue ? fmt::format(" = {}", toString(v->constantValue())) : "");

            break;
        }

        case parser::Kind::Type: {
            auto t = e->as<parser::Type>();

            if (t->isAlias) {
                fmt::print("type {} = {}\n", t->name, toTypeString(t->alias()));
            } else {
                std::vector<std::string> elements;

                auto fields = t->fields();
                elements.reserve(fields.size());

                for (auto field : fields) {
                    elements.push_back(fmt::format(
                        "{}{} {}", field->isMutable ? "var " : "", field->name, toTypeString(field->fixedType())));
                }

                fmt::print("type {} {{\n\t{}\n}}\n", t->name, fmt::join(elements, "\n\t"));
            }

            break;
        }

        default:
            throw;
        }
    }
}
