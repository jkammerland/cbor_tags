#pragma once

#include "include_tracker.hpp"
#include "visitor.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <utility>

class ASTConsumer : public clang::ASTConsumer {
  public:
    explicit ASTConsumer(clang::ASTContext *context, VisitCompleteCallback cb) : visitor_(context, cb) {}

    void HandleTranslationUnit(clang::ASTContext &context) override { visitor_.TraverseDecl(context.getTranslationUnitDecl()); }

    ~ASTConsumer() override = default;

  private:
    Visitor visitor_;
};

class DeclarationExtractorAction : public clang::ASTFrontendAction {
  public:
    explicit DeclarationExtractorAction(VisitCompleteCallback cb, HeaderCallback hcb) : cb_(std::move(cb)), hcb_(hcb) {}

    bool BeginSourceFileAction(clang::CompilerInstance &CI) override {
        // Create and register the IncludeTracker with the SourceManager
        CI.getPreprocessor().addPPCallbacks(std::make_unique<IncludeTracker>(CI.getSourceManager(), hcb_));
        return true;
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef file) override {
        llvm::outs() << "Processing file: " << file << "\n";
        auto fileName = file.find_last_of('/') != std::string::npos ? file.substr(file.find_last_of('/') + 1) : file;
        hcb_({{fileName.str(), file.str(), false, true}});
        consumer_ = new ASTConsumer(&compiler.getASTContext(), cb_);
        return std::unique_ptr<clang::ASTConsumer>(consumer_);
    }

    ~DeclarationExtractorAction() override = default;

  private:
    VisitCompleteCallback cb_;
    HeaderCallback        hcb_;
    ASTConsumer          *consumer_{};
};

// Create an action factory
class DeclarationExtractionActionFactory : public clang::tooling::FrontendActionFactory {
  public:
    explicit DeclarationExtractionActionFactory(VisitCompleteCallback cb, HeaderCallback hcb) : cb_(std::move(cb)), hcb_(std::move(hcb)) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        action_ = new DeclarationExtractorAction(cb_, hcb_);
        return std::unique_ptr<clang::FrontendAction>(action_);
    }

    ~DeclarationExtractionActionFactory() override = default;

  private:
    VisitCompleteCallback       cb_;
    HeaderCallback              hcb_;
    DeclarationExtractorAction *action_;
};
