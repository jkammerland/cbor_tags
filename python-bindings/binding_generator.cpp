// #include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <fmt/format.h>
#include <fstream>
#include <llvm/Support/CommandLine.h>
#include <memory>
#include <string>
#include <vector>

struct StructInfo {
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> members; // (type, name)
};

class StructVisitor : public clang::RecursiveASTVisitor<StructVisitor> {
  public:
    explicit StructVisitor(clang::ASTContext *context) : context_(context) {}

    bool VisitCXXRecordDecl(clang::CXXRecordDecl *declaration) {
        if (declaration->isStruct() && !declaration->isImplicit()) {
            StructInfo info;
            info.name = declaration->getNameAsString();

            // Collect member variables
            for (const auto *field : declaration->fields()) {
                std::string type = field->getType().getAsString();
                std::string name = field->getNameAsString();
                info.members.emplace_back(type, name);
            }

            structs_.push_back(info);
        }
        return true;
    }

    const std::vector<StructInfo> &getStructs() const { return structs_; }

  private:
    clang::ASTContext      *context_;
    std::vector<StructInfo> structs_;
};

class StructASTConsumer : public clang::ASTConsumer {
  public:
    explicit StructASTConsumer(clang::ASTContext *context) : visitor_(context) {}

    void HandleTranslationUnit(clang::ASTContext &context) override { visitor_.TraverseDecl(context.getTranslationUnitDecl()); }

    const std::vector<StructInfo> &getStructs() const { return visitor_.getStructs(); }

  private:
    StructVisitor visitor_;
};

class BindingGeneratorAction : public clang::ASTFrontendAction {
  public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef file) override {
        return std::make_unique<StructASTConsumer>(&compiler.getASTContext());
    }
};

void generateBindings(const std::vector<StructInfo> &structs, const std::string &moduleName) {
    std::ofstream out(moduleName + "_bindings.cpp");

    // Write header
    out << R"(#include <pybind11/pybind11.h>
namespace py = pybind11;

)" << std::endl;

    // Include all necessary headers
    // Note: You might need to modify this part based on your actual header locations
    for (const auto &structInfo : structs) {
        out << fmt::format("#include \"{}.h\"\n", structInfo.name);
    }

    out << "\nPYBIND11_MODULE(" << moduleName << ", m) {\n";

    // Generate bindings for each struct
    for (const auto &structInfo : structs) {
        out << fmt::format("    py::class_<{0}>(m, \"{0}\")\n", structInfo.name);
        out << "        .def(py::init<>())  // Default constructor\n";

        // Add members
        for (const auto &[type, name] : structInfo.members) {
            out << fmt::format("        .def_readwrite(\"{0}\", &{1}::{0})\n", name, structInfo.name);
        }
        out << "        ;\n\n";
    }

    out << "}\n";
}

int main(int argc, const char **argv) {
    if (argc < 3) {
        llvm::errs() << "Usage: " << argv[0] << " <module-name> <source-file> [compiler-args...]\n";
        return 1;
    }

    std::string moduleName = argv[1];

    // Setup Clang tool
    auto expectedParser = clang::tooling::CommonOptionsParser::create(argc, argv, llvm::cl::getGeneralCategory());
    if (!expectedParser) {
        llvm::errs() << "Error parsing command line arguments\n";
        return 1;
    }

    clang::tooling::ClangTool tool(expectedParser->getCompilations(), expectedParser->getSourcePathList());

    // Run the Clang tool
    std::vector<StructInfo> structs;
    tool.run(clang::tooling::newFrontendActionFactory<BindingGeneratorAction>().get());

    // Generate bindings
    generateBindings(structs, moduleName);

    return 0;
}