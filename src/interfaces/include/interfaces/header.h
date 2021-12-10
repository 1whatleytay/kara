#pragma once

// Internal include, you'll need to link against clang-visitor to access this
// stuff.

#include <hermes/node.h>

#include <clang/AST/AST.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Tooling/Tooling.h>

using namespace hermes;

namespace kara::parser {
    struct Root;
}

namespace kara::interfaces::header {
    struct TranslateFactory;

    struct TranslateVisitor : clang::RecursiveASTVisitor<TranslateVisitor> {
        TranslateFactory *factory;
        clang::ASTContext &context;

        std::unique_ptr<Node> make(Node *parent, const clang::QualType &wrapper, bool inStruct = false) const;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "HidingNonVirtualFunction"
        [[maybe_unused]] bool VisitVarDecl(clang::VarDecl *decl) const;
        [[maybe_unused]] bool VisitTypedefDecl(clang::TypedefDecl *decl) const;
        [[maybe_unused]] bool VisitFunctionDecl(clang::FunctionDecl *decl) const;
#pragma clang diagnostic pop

        explicit TranslateVisitor(clang::ASTContext &context, TranslateFactory *factory);
    };

    struct TranslatePreprocessorCallback : public clang::PPCallbacks {
        TranslateFactory &factory;
        clang::CompilerInstance &compiler;

        void MacroDefined(const clang::Token &token, const clang::MacroDirective *macro) override;

        explicit TranslatePreprocessorCallback(clang::CompilerInstance &compiler, TranslateFactory &factory);
    };

    struct TranslateConsumer : public clang::ASTConsumer {
        TranslateFactory *factory;

        void HandleTranslationUnit(clang::ASTContext &context) override;

        explicit TranslateConsumer(TranslateFactory *factory);
    };

    struct TranslateAction : clang::ASTFrontendAction {
        TranslateFactory *factory;

        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &compiler, llvm::StringRef file) override;

        explicit TranslateAction(TranslateFactory *factory);
    };

    struct TranslateFactory : public clang::tooling::FrontendActionFactory {
        size_t id = 0;

        parser::Root *node;
        std::unordered_map<clang::EnumDecl *, std::string> prebuiltEnums;
        std::unordered_map<clang::RecordDecl *, std::string> prebuiltTypes;

        std::unique_ptr<clang::FrontendAction> create() override;

        explicit TranslateFactory(parser::Root *node);
    };
}
