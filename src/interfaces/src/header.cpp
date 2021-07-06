#include <interfaces/header.h>

#include <interfaces/interfaces.h>

#include <parser/root.h>
#include <parser/type.h>
#include <parser/function.h>
#include <parser/literals.h>
#include <parser/variable.h>

#include <clang/Lex/LiteralSupport.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/CompilerInstance.h>
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


    void TranslatePreprocessorCallback::MacroDefined(const Token &token, const MacroDirective *macro) {
        auto name = token.getIdentifierInfo()->getName();

        auto info = macro->getMacroInfo();

        if (info->getNumTokens() != 1)
            return;

        auto t = info->getReplacementToken(0);

        if (t.is(tok::numeric_constant)) {
            NumericLiteralParser parser(
                std::string_view(t.getLiteralData(), t.getLength()), macro->getLocation(),
                compiler.getSourceManager(), compiler.getLangOpts(),
                compiler.getTarget(), compiler.getDiagnostics());

            if (!parser.isFloat) {
                llvm::APInt ap(64, 0);
                parser.GetIntegerValue(ap);

                auto varNode = std::make_unique<VariableNode>(factory.node, false, true);

                varNode->name = name;
                varNode->isMutable = false;
                varNode->hasFixedType = true;
                varNode->hasConstantValue = true;

                auto primNode = std::make_unique<PrimitiveTypenameNode>(varNode.get(), true);
                primNode->type = parser.isUnsigned ? PrimitiveType::ULong : PrimitiveType::Long;

                auto numberNode = std::make_unique<NumberNode>(varNode.get(), true);
                if (parser.isUnsigned)
                    numberNode->value = ap.getZExtValue();
                else
                    numberNode->value = ap.getSExtValue();

                varNode->children.push_back(std::move(primNode));
                varNode->children.push_back(std::move(numberNode));

                factory.node->children.push_back(std::move(varNode));
            }// else {
//                fmt::print("Skipping token {}, float: {}, unsigned: {}\n", name, (bool)parser.isFloat, (bool)parser.isUnsigned);
//            }
        }
    }

    TranslatePreprocessorCallback::TranslatePreprocessorCallback(CompilerInstance &compiler, TranslateFactory &factory)
        : compiler(compiler), factory(factory) { }

    std::unique_ptr<Node> TranslateVisitor::make( // NOLINT(misc-no-recursion)
        Node *parent, const clang::QualType &wrapper, bool inStruct) const {
        const clang::Type &type = *wrapper;

        std::string z = wrapper.getAsString();

        auto die = [&wrapper]() {
            fmt::print("Cannot translate type {}.\n", wrapper.getAsString());

            return nullptr;
        };

        if (type.isRecordType()) {
            auto record = type.castAs<RecordType>()->getDecl();

            auto iterator = factory->prebuiltTypes.find(record);

            std::string typeName;

            if (iterator != factory->prebuiltTypes.end()) {
                typeName = iterator->second;
            } else {
                auto fields = record->fields();

                auto typeNode = std::make_unique<TypeNode>(factory->node, true);

                typeNode->name = record->getNameAsString();
                if (typeNode->name.empty())
                    typeNode->name = fmt::format("_HeaderGen{}", factory->id++);

                factory->prebuiltTypes[record] = typeNode->name;

                for (auto field : fields) {
                    auto varNode = std::make_unique<VariableNode>(typeNode.get(), false, true);

                    varNode->name = field->getNameAsString();
                    varNode->isMutable = true;
                    varNode->hasFixedType = true;

                    auto val = make(varNode.get(), field->getType(), true);

                    if (!val)
                        return die();

                    varNode->children.push_back(std::move(val));

                    typeNode->children.push_back(std::move(varNode));
                }

                typeName = typeNode->name;
                factory->node->children.push_back(std::move(typeNode));
            }

            auto named = std::make_unique<NamedTypenameNode>(parent, true);
            named->name = typeName;

            return named;
        }

        if (type.isArrayType()) {
            auto e = type.castAsArrayTypeUnsafe();

            if (!e->isConstantArrayType())
                return die();

            auto constant = reinterpret_cast<const ConstantArrayType *>(e);
            auto size = constant->getSize().getZExtValue();

            assert(inStruct);

            auto arr = std::make_unique<ArrayTypenameNode>(parent, true);
            arr->type = ArrayKind::FixedSize;

            auto num = std::make_unique<NumberNode>(arr.get(), true);
            num->value = size;

            arr->children.push_back(make(arr.get(), constant->getElementType(), true));
            arr->children.push_back(std::move(num));

            return arr;
        }

        if (type.isPointerType()) {
            auto e = type.castAs<PointerType>();

            auto pointee = e->getPointeeType();

            auto ref = std::make_unique<ReferenceTypenameNode>(parent, true);

            if (type.isVoidPointerType()) {
                auto prim = std::make_unique<PrimitiveTypenameNode>(ref.get(), true);

                prim->type = PrimitiveType::Any;
                ref->children.push_back(std::move(prim));

                return ref;
            }

            ArrayTypenameNode *arr = nullptr;

            {
                auto arrPtr = std::make_unique<ArrayTypenameNode>(ref.get(), true);
                arr = arrPtr.get();

                ref->children.push_back(std::move(arrPtr));
            }

            arr->type = ArrayKind::Unbounded;
            ref->isMutable = !pointee.isConstQualified();

            auto subtype = make(arr, pointee);

            if (subtype) {
                arr->children.push_back(std::move(subtype));
            } else {
                auto prim = std::make_unique<PrimitiveTypenameNode>(arr, true);
                prim->type = PrimitiveType::Nothing;

                arr->children.push_back(std::move(prim));
            }

            return ref;
        }

        if (type.isBuiltinType()) {
            auto e = type.castAs<BuiltinType>();

            BuiltinType::Kind kind = e->getKind();
            size_t size = context.getTypeSize(wrapper);

            auto prim = std::make_unique<PrimitiveTypenameNode>(parent, true);

            if (kind == BuiltinType::Kind::Void) {
                prim->type = PrimitiveType::Nothing;
            } else if (kind == BuiltinType::Kind::Bool) {
                prim->type = PrimitiveType::Bool;
            } else if (type.isIntegerType()) {
                if (type.isSignedIntegerType()) {
                    switch (size) {
                        case 8: prim->type = PrimitiveType::Byte; break;
                        case 16: prim->type = PrimitiveType::Short; break;
                        case 32: prim->type = PrimitiveType::Int; break;
                        case 64: prim->type = PrimitiveType::Long; break;
                        default: return die();
                    }
                } else {
                    switch (size) {
                        case 8: prim->type = PrimitiveType::UByte; break;
                        case 16: prim->type = PrimitiveType::UShort; break;
                        case 32: prim->type = PrimitiveType::UInt; break;
                        case 64: prim->type = PrimitiveType::ULong; break;
                        default: return die();
                    }
                }
            } else if (type.isRealFloatingType()) {
                switch (size) {
                    case 32: prim->type = PrimitiveType::Float; break;
                    case 64: prim->type = PrimitiveType::Double; break;
                    default: return die();
                }
            } else {
                prim = nullptr;
            }

            if (prim)
                return prim;
        }

        return die();
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"
#pragma ide diagnostic ignored "HidingNonVirtualFunction"
    bool TranslateVisitor::VisitVarDecl(VarDecl *decl) const {
        if (!decl->hasGlobalStorage())
            return true;

        if (!decl->hasExternalStorage()) {
            fmt::print("Skipping constructing variable {}, must be external.\n", decl->getNameAsString());
            return true;
        }

        auto var = std::make_unique<VariableNode>(factory->node, false, true);

        var->name = decl->getNameAsString();
        assert(!var->name.empty());

        var->isMutable = true;
        var->isExternal = true;
        var->hasFixedType = true;

        auto type = make(var.get(), decl->getType(), true);

        if (!type) {
            fmt::print("Skipping constructing variable {}.\n", decl->getNameAsString());
            return true;
        }

        var->children.push_back(std::move(type));

        factory->node->children.push_back(std::move(var));

        return true;
    }

    [[maybe_unused]] bool TranslateVisitor::VisitTypedefDecl(TypedefDecl *decl) const {
        auto type = std::make_unique<TypeNode>(factory->node, true);

        type->name = decl->getNameAsString();
        type->isAlias = true;

        auto underlyingType = make(type.get(), decl->getUnderlyingType(), true);

        if (!underlyingType) {
            fmt::print("Cannot construct typedef {}.\n", decl->getNameAsString());
            return true;
        }

        type->children.push_back(std::move(underlyingType));
        factory->node->children.push_back(std::move(type));

        return true;
    }

    [[maybe_unused]] bool TranslateVisitor::VisitFunctionDecl(FunctionDecl *decl) const {
        auto name = decl->getNameAsString();
        auto parameters = decl->parameters();

        auto function = std::make_unique<FunctionNode>(factory->node, true);

        auto returnType = make(function.get(), decl->getReturnType());

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

            auto optional = make(var, param->getType());

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

    TranslateVisitor::TranslateVisitor(clang::ASTContext &context, TranslateFactory *factory)
        : context(context), factory(factory) { }

    void TranslateConsumer::HandleTranslationUnit(clang::ASTContext &context) {
        TranslateVisitor visitor(context, factory); // if this is expensive :shrug:

        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }

    TranslateConsumer::TranslateConsumer(TranslateFactory *factory) : factory(factory) { }

    std::unique_ptr<clang::ASTConsumer> TranslateAction::CreateASTConsumer(
        clang::CompilerInstance &compiler, llvm::StringRef file) {
        assert(compiler.hasSourceManager());

        compiler.getPreprocessor().addPPCallbacks(std::make_unique<TranslatePreprocessorCallback>(compiler, *factory));

        return std::make_unique<TranslateConsumer>(factory);
    }

    TranslateAction::TranslateAction(TranslateFactory *factory) : factory(factory) { }

    std::unique_ptr<clang::FrontendAction> TranslateFactory::create() {
        return std::make_unique<TranslateAction>(this);
    }

    TranslateFactory::TranslateFactory(RootNode *node) : node(node) { }

    InterfaceResult create(int count, const char **args) {
        auto parser = clang::tooling::CommonOptionsParser::create(
            count, args, llvm::cl::GeneralCategory, llvm::cl::OneOrMore);
        if (!parser)
            throw std::runtime_error(toString(parser.takeError()));

        clang::tooling::ClangTool tool(parser->getCompilations(), parser->getSourcePathList());

        auto state = std::make_unique<State>("");
        auto node = std::make_unique<RootNode>(*state, true);

        interfaces::header::TranslateFactory factory(node.get());

        tool.run(&factory);

        return { std::move(state), std::move(node) };
    }
}
