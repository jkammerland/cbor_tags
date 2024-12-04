#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <fmt/format.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

struct DeclarationName {
    std::string                plain;
    std::string                qualified;
    std::optional<std::string> namespace_;

    [[nodiscard]] bool hasNamespace() const noexcept { return namespace_.has_value(); }
};

struct FieldDeclarationInfo {
    DeclarationName type;
    DeclarationName name;
    bool            isConst{false};
    bool            isPointer{false};
    bool            isReference{false};
    bool            isStatic{false};
    bool            isConstexpr{false};

    [[nodiscard]] constexpr bool isSpecial() const noexcept { return isConst || isPointer || isReference || isStatic || isConstexpr; }
};

struct StructInfo {
    DeclarationName                   name;
    bool                              isEnum{false};
    std::vector<FieldDeclarationInfo> members;

    [[nodiscard]] bool   empty() const noexcept { return members.empty(); }
    [[nodiscard]] size_t memberCount() const noexcept { return members.size(); }
};

struct FunctionInfo {
    DeclarationName                   name;
    DeclarationName                   returnType;
    bool                              isMemberFunction{false};
    bool                              isPureVirtual{false};
    bool                              isStatic{false};
    std::optional<DeclarationName>    parent;
    std::vector<FieldDeclarationInfo> parameters;

    [[nodiscard]] bool hasParameters() const noexcept { return !parameters.empty(); }
};

using Structs   = std::vector<StructInfo>;
using Functions = std::vector<FunctionInfo>;

using VisitCompleteCallback = std::function<void(Structs &&, Functions &&)>;

class Visitor : public clang::RecursiveASTVisitor<Visitor> {
  public:
    explicit Visitor(clang::ASTContext *context, VisitCompleteCallback cb) : context_(context), cb_(std::move(cb)) {}

    /**
     * @brief Filters the qualified name of a given declaration.
     *
     * This function takes a declaration and filters its qualified name to determine
     * if it belongs to non-user code (e.g., standard library, compiler-generated, etc.).
     * It returns a fieldInfo consisting of a boolean indicating whether the declaration is
     * non-user code and a string view of the qualified name.
     *
     * @tparam DeclarationType The type of the declaration.
     * @param declaration Pointer to the declaration to be filtered.
     * @return std::fieldInfo<bool, std::string_view> A fieldInfo where the first element is a boolean
     *         indicating if the declaration is non-user code, and the second element is a
     *         string view of the qualified name.
     * @note The qualified name is stored in the stringBuffer_ member variable, and must be consumed before next visit.
     */
    template <typename DeclarationType> auto FilterQualifiedName(const DeclarationType *declaration) {
        auto stringStream_ = getNewStream();
        declaration->printQualifiedName(stringStream_);
        auto &qName = stringBuffer_;

        auto isNonUserCode = qName.starts_with("std") || qName.starts_with("__") || declaration->isImplicit() ||
                             !declaration->isFirstDecl() || declaration->getLocation().isInvalid() || qName.empty() ||
                             context_->getSourceManager().isInSystemHeader(declaration->getLocation());
        return std::make_pair(isNonUserCode, std::string_view{qName});
    }

    inline auto getNewStream() -> llvm::raw_string_ostream {
        stringBuffer_.clear();
        return llvm::raw_string_ostream{stringBuffer_};
    }

    bool VisitCXXRecordDecl(clang::CXXRecordDecl *declaration) {

        auto [isNonUserCode, qName] = FilterQualifiedName(declaration);
        if (isNonUserCode) {
            return true;
        }

        StructInfo info;

        const clang::DeclContext *declContext = declaration->getDeclContext();
        if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) { /* Namespace */
            llvm::outs() << "[ NAMESPACE: " << namespaceDecl->getName() << " ]";
            info.name.namespace_ = namespaceDecl->getName();
        } else {
            llvm::outs() << "[ GLOBAL ]";
            info.name.namespace_ = std::nullopt;
        }

        llvm::outs() << declaration->getName() << "\n";
        info.name.plain     = declaration->getName();
        info.name.qualified = declaration->getQualifiedNameAsString();

        for (const auto *field : declaration->fields()) {
            FieldDeclarationInfo fieldInfo;
            fieldInfo.type.plain     = field->getType().getAsString();
            fieldInfo.type.qualified = field->getType().getCanonicalType().getAsString();
            fieldInfo.name.plain     = field->getName();
            fieldInfo.name.qualified = field->getQualifiedNameAsString();
            info.members.emplace_back(fieldInfo);
        }
        structs_.push_back(info);

        return true;
    }

    bool VisitEnumDecl(clang::EnumDecl *declaration) {
        auto [isNonUserCode, qName] = FilterQualifiedName(declaration);
        if (isNonUserCode) {
            return true;
        }

        StructInfo info;
        info.isEnum         = true;
        info.name.plain     = declaration->getName();
        info.name.qualified = declaration->getQualifiedNameAsString();

        for (const auto *enumerator : declaration->enumerators()) {
            FieldDeclarationInfo fieldInfo;
            fieldInfo.type.plain     = declaration->getIntegerType().getAsString();
            fieldInfo.type.qualified = declaration->getIntegerType().getCanonicalType().getAsString();
            fieldInfo.name.plain     = enumerator->getName();
            fieldInfo.name.qualified = enumerator->getQualifiedNameAsString();
            info.members.emplace_back(fieldInfo);
        }

        structs_.push_back(info);

        return true;
    }

    bool VisitCXXMethodDecl(clang::CXXMethodDecl *declaration) {
        auto [isNonUserCode, qName] = FilterQualifiedName(declaration);
        if (isNonUserCode) {
            return true;
        }
        llvm::outs() << "Struct: " << declaration->getParent()->getQualifiedNameAsString() << " methods: ";
        llvm::outs() << " " << declaration->getQualifiedNameAsString() << " ";
        llvm::outs() << "isPureVirtual: " << (declaration->isPureVirtual() ? "<yes>" : "<no>") << "\n";
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl *declaration) {
        auto [isNonUserCode, qName] = FilterQualifiedName(declaration);
        if (isNonUserCode) {
            return true;
        }

        FunctionInfo info;

        if (auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(declaration)) {
            info.parent            = DeclarationName{};
            info.isMemberFunction  = true;
            info.parent->plain     = method->getParent()->getName();
            info.parent->qualified = method->getParent()->getQualifiedNameAsString();
            auto *declContext      = method->getParent()->getDeclContext();
            if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) {
                info.parent->namespace_ = namespaceDecl->getName();
            } else {
                info.parent->namespace_ = std::nullopt;
            }
        } else {
            info.parent           = std::nullopt;
            info.isMemberFunction = false;
        }

        info.name.plain     = declaration->getName();
        info.name.qualified = declaration->getQualifiedNameAsString();
        info.isPureVirtual  = declaration->isPureVirtual();
        info.isStatic       = declaration->isStatic();

        info.returnType.plain     = declaration->getReturnType().getAsString();
        info.returnType.qualified = declaration->getReturnType().getAsString();

        for (const auto *param : declaration->parameters()) {
            FieldDeclarationInfo fieldInfo;
            fieldInfo.type.plain     = param->getType().getAsString();
            fieldInfo.type.qualified = param->getType().getCanonicalType().getAsString();
            fieldInfo.name.plain     = param->getName();
            fieldInfo.name.qualified = param->getQualifiedNameAsString();
            info.parameters.emplace_back(fieldInfo);
        }
        functions_.push_back(info);

        return true;
    }

    ~Visitor() { cb_(std::move(structs_), std::move(functions_)); }

  private:
    clang::ASTContext    *context_;
    VisitCompleteCallback cb_;
    std::string           stringBuffer_;

    std::vector<StructInfo>   structs_;
    std::vector<FunctionInfo> functions_;
};