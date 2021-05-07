#include <interfaces/header.h>

#include <interfaces/interfaces.h>

#include <parser/root.h>
#include <parser/function.h>
#include <parser/variable.h>

#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>

#include <fmt/printf.h>

using namespace clang;

namespace interfaces::header {
    template <typename T, typename ...Args>
    T *grab(Node *node, Args ... args) {
        auto t = node->pick<T>(false, args...);
        auto *ptr = t.get();

        node->children.push_back(std::move(t));

        return ptr;
    }

    std::optional<Typename> make(const clang::ASTContext &context, const clang::QualType &wrapper) {
        const clang::Type &type = *wrapper;

        if (type.isPointerType()) {
            const auto &e = reinterpret_cast<const PointerType &>(type);

            auto pointee = type.getPointeeType();

            std::optional<Typename> subtype = make(context, pointee);

            if (subtype.has_value()) {
                return ReferenceTypename {
                    std::make_shared<Typename>(ArrayTypename {
                        ArrayTypename::Kind::Unbounded,
                        std::make_shared<Typename>(std::move(subtype.value()))
                    }),
                    !pointee.isConstQualified()
                };
            }
        }

        if (type.isBuiltinType()) {
            const auto &e = reinterpret_cast<const BuiltinType &>(type);

            BuiltinType::Kind kind = e.getKind();
            size_t size = context.getTypeSize(wrapper);

            if (kind == BuiltinType::Kind::Void)
                return types::nothing();

            if (kind == BuiltinType::Kind::Bool)
                return types::boolean();

            if (type.isIntegerType()) {
                if (type.isSignedIntegerType()) {
                    switch (size) {
                        case 8: return types::i8();
                        case 16: return types::i16();
                        case 32: return types::i32();
                        case 64: return types::i64();
                        default: break;
                    }
                } else {
                    switch (size) {
                        case 8: return types::u8();
                        case 16: return types::u16();
                        case 32: return types::u32();
                        case 64: return types::u64();
                        default: break;
                    }
                }
            }
        }

        fmt::print("Cannot translate type {}.\n", wrapper.getAsString());

        return std::nullopt;
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "HidingNonVirtualFunction"

    [[maybe_unused]] bool TranslateVisitor::VisitFunctionDecl(FunctionDecl *decl) const {
        auto name = decl->getNameAsString();
        auto parameters = decl->parameters();
        auto returnType = make(context, decl->getReturnType());

        if (!returnType.has_value()) {
            fmt::print("Skipping translating function {}.\n", name);
            return true;
        }

        auto function = std::make_unique<FunctionNode>(factory->node, true);

        function->name = name;
        function->isExtern = true;
        function->parameterCount = parameters.size();
        function->returnType = *returnType;

        size_t id = 0;

        for (ParmVarDecl *param : parameters) {
            auto paramName = param->getNameAsString();

            auto *var = grab<VariableNode>(function.get(), true, true);

            auto optional = make(context, param->getType());

            if (!optional.has_value()) {
                fmt::print("Skipping translating function {}.\n", name);
                return true;
            }

            if (paramName.empty())
                paramName = fmt::format("unk{}", id++);

            var->name = paramName;
            var->isMutable = true;
            var->fixedType = make(context, param->getType());
        }

        factory->node->children.push_back(std::move(function));

        return true;
    }
#pragma clang diagnostic pop

    InterfaceResult create(int count, const char **args) {
        auto parser = clang::tooling::CommonOptionsParser::create(
            count, args, llvm::cl::GeneralCategory, llvm::cl::OneOrMore);
        if (auto error = parser.takeError())
            throw std::runtime_error(toString(std::move(error)));

        clang::tooling::ClangTool tool(parser->getCompilations(), parser->getSourcePathList());

        auto state = std::make_unique<State>("");
        auto node = std::make_unique<RootNode>(*state, true);

        interfaces::header::TranslateFactory factory(node.get());

        tool.run(&factory);

        return { std::move(state), std::move(node) };
    }
}
