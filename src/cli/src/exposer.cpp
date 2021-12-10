#include <cli/exposer.h>

#include <parser/type.h>
#include <parser/root.h>
#include <parser/literals.h>
#include <parser/typename.h>
#include <parser/variable.h>
#include <parser/function.h>

#include <utils/typename.h>

#include <yaml-cpp/yaml.h>

#include <fmt/format.h>

namespace kara::cli {
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

            return fmt::format("func{}{}({}) {}", locked, flags,
                fmt::join(text, ", "), toTypeString(e->returnType()));
        }

        default:
            throw;
        }
    }

    void expose(const parser::Root *root, YAML::Emitter &emitter) {
        emitter << YAML::BeginSeq;

        for (const auto &e : root->children) {
            switch (e->is<parser::Kind>()) {
            case parser::Kind::Function: {
                auto f = e->as<parser::Function>();

                emitter << YAML::BeginMap;
                emitter << YAML::Key << "kind" << YAML::Value << "function";
                emitter << YAML::Key << "name" << YAML::Value << f->name;
                emitter << YAML::Key << "parameters" << YAML::Value;
                emitter << YAML::BeginSeq;

                for (size_t a = 0; a < f->parameterCount; a++) {
                    auto *v = f->children[a]->as<parser::Variable>();

                    assert(v->hasFixedType);

                    emitter << YAML::BeginMap;
                    emitter << YAML::Key << "name" << YAML::Value << v->name;
                    emitter << YAML::Key << "type" << YAML::Value << toTypeString(v->fixedType());
                    emitter << YAML::EndMap;
                }

                emitter << YAML::EndSeq;
                emitter << YAML::Key << "return-type" << YAML::Value << toTypeString(f->fixedType());
                emitter << YAML::Key << "external" << YAML::Value << f->isExtern;
                emitter << YAML::Key << "c-var-args" << YAML::Value << f->isCVarArgs;
                emitter << YAML::EndMap;

                break;
            }

            case parser::Kind::Variable: {
                auto v = e->as<parser::Variable>();

                auto toString = [](const parser::Number *node) -> std::string {
                    return std::visit([](auto x) { return std::to_string(x); }, node->value);
                };

                assert(!v->hasInitialValue);

                emitter << YAML::BeginMap;
                emitter << YAML::Key << "kind" << YAML::Value << "variable";
                emitter << YAML::Key << "name" << YAML::Value << v->name;
                emitter << YAML::Key << "mutable" << YAML::Value << v->isMutable;
                emitter << YAML::Key << "type" << YAML::Value << toTypeString(v->fixedType());
                emitter << YAML::Key << "external" << YAML::Value << v->isExternal;
                emitter << YAML::Key << "constant-value" << YAML::Value;
                if (v->hasConstantValue)
                    emitter << toString(v->constantValue());
                else
                    emitter << YAML::Null;
                emitter << YAML::EndMap;

                break;
            }

            case parser::Kind::Type: {
                auto t = e->as<parser::Type>();

                if (t->isAlias) {
                    emitter << YAML::BeginMap;
                    emitter << YAML::Key << "kind" << YAML::Value << "type-alias";
                    emitter << YAML::Key << "name" << YAML::Value << t->name;
                    emitter << YAML::Key << "type" << YAML::Value << toTypeString(t->alias());
                    emitter << YAML::EndMap;
                } else {
                    emitter << YAML::BeginMap;
                    emitter << YAML::Key << "kind" << YAML::Value << "type";
                    emitter << YAML::Key << "name" << YAML::Value << t->name;
                    emitter << YAML::Key << "fields" << YAML::Value;
                    emitter << YAML::BeginSeq;

                    auto fields = t->fields();

                    for (auto field : fields) {
                        emitter << YAML::BeginMap;
                        emitter << YAML::Key << "name" << YAML::Value << field->name;
                        emitter << YAML::Key << "type" << YAML::Value << toTypeString(field->fixedType());
                        emitter << YAML::Key << "mutable" << YAML::Value << field->isMutable;
                        emitter << YAML::EndMap;
                    }

                    emitter << YAML::EndSeq;
                    emitter << YAML::EndMap;
                }

                break;
            }

            default:
                throw;
            }
        }

        emitter << YAML::EndSeq;
    }
}