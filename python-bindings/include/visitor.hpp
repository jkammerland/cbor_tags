#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <utility>

struct DeclarationName {
    std::string plain;
    std::string qualified;
};

struct StructInfo {
    std::string                                      name;
    std::string                                      qualifiedName;
    std::vector<std::pair<std::string, std::string>> members;
};

struct FunctionInfo {
    std::string                                      name;
    std::string                                      qualifiedName;
    std::string                                      returnType;
    std::vector<std::pair<std::string, std::string>> parameters;
};

using Structs   = std::vector<StructInfo>;
using Functions = std::vector<FunctionInfo>;

using VisitCompleteCallback = std::function<void(Structs &&, Functions &&)>;

class Visitor : public clang::RecursiveASTVisitor<Visitor> {
  public:
    explicit Visitor(clang::ASTContext *context, VisitCompleteCallback cb) : context_(context), cb_(std::move(cb)) {}

    // 1. Starts with std or dual underscore
    // 2. Is in system header
    // 3. Has no name (anonymous)
    // 4. Is implicit
    template <typename DeclarationType> auto FilterQualifiedName(const DeclarationType *declaration) {
        auto stringStream_ = getNewStream();
        declaration->printQualifiedName(stringStream_);
        auto &qName = stringBuffer_;

        auto isNonUserCode = qName.starts_with("std") || qName.starts_with("__") || declaration->isImplicit() ||
                             declaration->getLocation().isInvalid() || qName.empty() ||
                             context_->getSourceManager().isInSystemHeader(declaration->getLocation());
        return std::make_pair(isNonUserCode, std::string_view{qName});
    }

    inline auto getNewStream() -> llvm::raw_string_ostream {
        stringBuffer_.clear();
        return llvm::raw_string_ostream{stringBuffer_};
    }

    bool VisitCXXRecordDecl(clang::CXXRecordDecl *declaration) {
        // declaration->dump();

        auto [isNonUserCode, qName] = FilterQualifiedName(declaration);
        if (isNonUserCode) {
            return true;
        }

        const clang::DeclContext *declContext = declaration->getDeclContext();
        if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) { /* Namespace */
            llvm::outs() << "[ NAMESPACE: " << namespaceDecl->getName() << " ]";
        }

        StructInfo info;

        llvm::outs() << declaration->getName() << "\n";
        info.name = declaration->getQualifiedNameAsString();
        for (const auto *field : declaration->fields()) {
            std::string type          = field->getType().getAsString();
            std::string qualifiedName = field->getQualifiedNameAsString();
            std::string name          = field->getNameAsString();
            info.members.emplace_back(type, qualifiedName);
        }
        structs_.push_back(info);

        return true;
    }

    bool VisitCXXMethodDecl(clang::CXXMethodDecl *declaration) {
        auto [isNonUserCode, qName] = FilterQualifiedName(declaration);
        if (isNonUserCode) {
            return true;
        }

        llvm::outs() << "VisitCXXMethodDecl: " << declaration->getQualifiedNameAsString() << "\n";
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl *declaration) {
        auto [isNonUserCode, qName] = FilterQualifiedName(declaration);
        if (isNonUserCode) {
            return true;
        }

        FunctionInfo info;
        info.name       = declaration->getNameAsString();
        info.returnType = declaration->getReturnType().getAsString();

        for (const auto *param : declaration->parameters()) {
            std::string type = param->getType().getAsString();
            std::string name = param->getNameAsString();
            info.parameters.emplace_back(type, name);
        }
        functions_.push_back(info);

        return true;
    }

    ~Visitor() {
        cb_(std::move(structs_), std::move(functions_));
        // llvm::outs() << "Destroying StructVisitor\n";
    }

  private:
    clang::ASTContext    *context_;
    VisitCompleteCallback cb_;
    std::string           stringBuffer_;

    std::vector<StructInfo>   structs_;
    std::vector<FunctionInfo> functions_;
};