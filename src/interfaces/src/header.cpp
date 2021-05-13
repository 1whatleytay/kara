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

    std::unique_ptr<Node> make(Node *parent, const clang::ASTContext &context, const clang::QualType &wrapper) {
        const clang::Type &type = *wrapper;

        if (type.isPointerType()) {
            const auto &e = reinterpret_cast<const PointerType &>(type);

            auto pointee = type.getPointeeType();

            auto ref = std::make_unique<ReferenceTypenameNode>(parent, true);

            ArrayTypenameNode *arr = nullptr;

            {
                auto arrPtr = std::make_unique<ArrayTypenameNode>(ref.get(), true);
                arr = arrPtr.get();

                ref->children.push_back(std::move(arrPtr));
            }

            arr->type = ArrayKind::Unbounded;
            ref->isMutable = !pointee.isConstQualified();

            if (type.isVoidPointerType()) {
                auto prim = std::make_unique<PrimitiveTypenameNode>(arr, true);

                prim->type = PrimitiveType::Any;
                arr->children.push_back(std::move(prim));

                return ref;
            }

            auto subtype = make(arr, context, pointee);

            if (subtype) {
                arr->children.push_back(std::move(subtype));

                return ref;
            }
        }

        if (type.isBuiltinType()) {
            const auto &e = reinterpret_cast<const BuiltinType &>(type);

            BuiltinType::Kind kind = e.getKind();
            size_t size = context.getTypeSize(wrapper);

            auto prim = std::make_unique<PrimitiveTypenameNode>(parent, true);

            if (kind == BuiltinType::Kind::Void) {
                prim->type = PrimitiveType::Nothing;
            } else if (kind == BuiltinType::Kind::Bool) {
                prim->type = PrimitiveType::Bool;
            } if (type.isIntegerType()) {
                if (type.isSignedIntegerType()) {
                    switch (size) {
                        case 8: prim->type = PrimitiveType::Byte; break;
                        case 16: prim->type = PrimitiveType::Short; break;
                        case 32: prim->type = PrimitiveType::Int; break;
                        case 64: prim->type = PrimitiveType::Long; break;
                        default: assert(false);
                    }
                } else {
                    switch (size) {
                        case 8: prim->type = PrimitiveType::UByte; break;
                        case 16: prim->type = PrimitiveType::UShort; break;
                        case 32: prim->type = PrimitiveType::UInt; break;
                        case 64: prim->type = PrimitiveType::ULong; break;
                        default: assert(false);
                    }
                }
            } else if (type.isRealFloatingType()) {
                switch (size) {
                    case 32: prim->type = PrimitiveType::Float; break;
                    case 64: prim->type = PrimitiveType::Double; break;
                    default: assert(false);
                }
            } else {
                prim = nullptr;
            }

            if (prim)
                return prim;
        }

        fmt::print("Cannot translate type {}.\n", wrapper.getAsString());

        return nullptr;
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"
#pragma ide diagnostic ignored "HidingNonVirtualFunction"
    [[maybe_unused]] bool TranslateVisitor::VisitFunctionDecl(FunctionDecl *decl) const {
        auto name = decl->getNameAsString();
        auto parameters = decl->parameters();

        auto function = std::make_unique<FunctionNode>(factory->node, true);

        auto returnType = make(function.get(), context, decl->getReturnType());

        if (!returnType) {
            fmt::print("Skipping translating function {}.\n", name);
            return true;
        }

        function->name = name;
        function->isExtern = true;
        function->hasFixedType = true;
        function->parameterCount = parameters.size();

        size_t id = 0;

        for (ParmVarDecl *param : parameters) {
            auto paramName = param->getNameAsString();

            // huh, useless function
            auto *var = grab<VariableNode>(function.get(), true, true);

            auto optional = make(var, context, param->getType());

            if (!optional) {
                fmt::print("Skipping translating function {}.\n", name);
                return true;
            }

            if (paramName.empty())
                paramName = fmt::format("unk{}", id++);

            var->name = paramName;
            var->isMutable = true;
            var->hasFixedType = true;
            var->children.push_back(std::move(optional));
        }

        function->children.push_back(std::move(returnType));
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
