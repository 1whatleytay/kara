#pragma once

// Internal include, you'll need to link against clang-visitor to access this stuff.

#include <clang/AST/AST.h>
#include <clang/Tooling/Tooling.h>
#include <clang/AST/RecursiveASTVisitor.h>

struct RootNode;

namespace interfaces::header {
    struct TranslateFactory;

    struct TranslateVisitor : clang::RecursiveASTVisitor<TranslateVisitor> {
        TranslateFactory *factory;
        clang::ASTContext &context;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "HidingNonVirtualFunction"

        [[maybe_unused]] bool VisitFunctionDecl(clang::FunctionDecl *decl) const;
#pragma clang diagnostic pop

        explicit TranslateVisitor(clang::ASTContext &context, TranslateFactory *factory)
            : context(context), factory(factory) { }
    };

    struct TranslateConsumer : public clang::ASTConsumer {
        TranslateFactory *factory;

        void HandleTranslationUnit(clang::ASTContext &context) override {
            TranslateVisitor visitor(context, factory); // if this is expensive :shrug:

            visitor.TraverseDecl(context.getTranslationUnitDecl());
        }

        explicit TranslateConsumer(TranslateFactory *factory) : factory(factory) { }
    };

    struct TranslateAction : clang::ASTFrontendAction {
        TranslateFactory *factory;

        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &compiler, llvm::StringRef file) override {

            return std::make_unique<TranslateConsumer>(factory);
        }

        explicit TranslateAction(TranslateFactory *factory) : factory(factory) { }
    };

    struct TranslateFactory : public clang::tooling::FrontendActionFactory {
        RootNode *node;

        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<TranslateAction>(this);
        }

        explicit TranslateFactory(RootNode *node) : node(node) { }
    };
}
