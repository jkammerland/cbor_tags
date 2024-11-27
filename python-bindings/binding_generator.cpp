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
#include <utility>
#include <vector>

struct StructInfo {
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> members;
};

struct FunctionInfo {
    std::string                                      name;
    std::string                                      returnType;
    std::vector<std::pair<std::string, std::string>> parameters;
};

using VisitCompleteCallback = std::function<void(std::vector<StructInfo> &&, std::vector<FunctionInfo> &&)>;

class StructVisitor : public clang::RecursiveASTVisitor<StructVisitor> {
  public:
    explicit StructVisitor(clang::ASTContext *context, VisitCompleteCallback cb) : context_(context), cb_(std::move(cb)) {}

    bool VisitCXXRecordDecl(clang::CXXRecordDecl *declaration) {
        // Check if it is a user-defined struct in the 'test' namespace
        if (declaration->isStruct() && !declaration->isImplicit()) {
            const clang::DeclContext *declContext = declaration->getDeclContext();
            if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) {
                if (namespaceDecl->getName() == "test") { // Only process structs in the 'test' namespace
                    llvm::outs() << "Struct: " << declaration->getName() << "\n";
                    StructInfo info;
                    info.name = declaration->getNameAsString();
                    for (const auto *field : declaration->fields()) {
                        std::string type = field->getType().getAsString();
                        std::string name = field->getNameAsString();
                        info.members.emplace_back(type, name);
                    }
                    structs_.push_back(info);
                }
            }
        }
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl *declaration) {
        // Only capture functions that are user-defined and in the 'test' namespace
        if (declaration->isFunctionOrMethod() && !declaration->isImplicit()) {
            const clang::DeclContext *declContext = declaration->getDeclContext();
            if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) {
                if (namespaceDecl->getName() == "test") { // Only process functions in the 'test' namespace
                    llvm::outs() << "Function: " << declaration->getName() << "\n";
                    FunctionInfo info;
                    info.name       = declaration->getNameAsString();
                    info.returnType = declaration->getReturnType().getAsString();

                    for (const auto *param : declaration->parameters()) {
                        std::string type = param->getType().getAsString();
                        std::string name = param->getNameAsString();
                        info.parameters.emplace_back(type, name);
                    }
                    functions_.push_back(info);
                }
            }
        }
        return true;
    }

    ~StructVisitor() {
        cb_(std::move(structs_), std::move(functions_));
        llvm::outs() << "Destroying StructVisitor\n";
    }

  private:
    [[maybe_unused]] clang::ASTContext *context_;
    VisitCompleteCallback               cb_;

    std::vector<StructInfo>   structs_;
    std::vector<FunctionInfo> functions_;
};

class StructASTConsumer : public clang::ASTConsumer {
  public:
    explicit StructASTConsumer(clang::ASTContext *context, VisitCompleteCallback cb) : visitor_(context, cb) {}

    void HandleTranslationUnit(clang::ASTContext &context) override { visitor_.TraverseDecl(context.getTranslationUnitDecl()); }

  private:
    StructVisitor visitor_;
};

class BindingGeneratorAction : public clang::ASTFrontendAction {
  public:
    explicit BindingGeneratorAction(VisitCompleteCallback cb) : cb_(std::move(cb)) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef file) override {
        fmt::print("Creating processing action for file: {}\n", file.str());
        consumer_ = new StructASTConsumer(&compiler.getASTContext(), cb_);
        return std::unique_ptr<clang::ASTConsumer>(consumer_);
    }

    ~BindingGeneratorAction() override { llvm::outs() << "Destroying BindingGeneratorAction\n"; }

  private:
    VisitCompleteCallback cb_;
    StructASTConsumer    *consumer_{};
};

void generateBindings(const std::vector<StructInfo> &structs, const std::vector<FunctionInfo> &functions, const std::string &moduleName) {

    std::ofstream out(moduleName + "_bindings.cpp");

    // Write headers
    out << "#include <pybind11/pybind11.h>\n"
        << "#include <pybind11/stl.h>\n"
        << "namespace py = pybind11;\n\n";

    // Include headers for structs and functions
    for (const auto &structInfo : structs) {
        out << fmt::format("#include \"{}.h\"\n", structInfo.name);
    }

    out << "\nPYBIND11_MODULE(" << moduleName << ", m) {\n";

    // Generate struct bindings
    for (const auto &structInfo : structs) {
        out << fmt::format("    py::class_<{0}>(m, \"{0}\")\n", structInfo.name) << "        .def(py::init<>())\n";

        for (const auto &[type, name] : structInfo.members) {
            out << fmt::format("        .def_readwrite(\"{0}\", &{1}::{0})\n", name, structInfo.name);
        }
        out << "        ;\n\n";
    }

    // Generate function bindings
    for (const auto &funcInfo : functions) {
        out << fmt::format("    m.def(\"{0}\", &{0});\n", funcInfo.name);
    }

    out << "}\n";
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        llvm::errs() << "Usage: " << argv[0] << "<source-file>... [compiler-args...]\n";
        return 1;
    }

    // Print all args
    for (int i = 0; i < argc; ++i) {
        llvm::outs() << "argv[" << i << "] = " << argv[i] << "\n";
    }

    std::string moduleName = "my_module";

    auto expectedParser = clang::tooling::CommonOptionsParser::create(argc, argv, llvm::cl::getGeneralCategory());
    if (!expectedParser) {
        llvm::errs() << "Error parsing command line arguments: " << expectedParser.takeError() << "\n";
        return 1;
    }

    clang::tooling::ClangTool tool(expectedParser->getCompilations(), expectedParser->getSourcePathList());

    // Create an action factory
    class BindingGeneratorActionFactory : public clang::tooling::FrontendActionFactory {
      public:
        explicit BindingGeneratorActionFactory(VisitCompleteCallback cb) : cb_(std::move(cb)) {}

        std::unique_ptr<clang::FrontendAction> create() override {
            action_ = new BindingGeneratorAction(cb_);
            return std::unique_ptr<clang::FrontendAction>(action_);
        }

        BindingGeneratorAction *getAction() const { return action_; }

      private:
        VisitCompleteCallback   cb_;
        BindingGeneratorAction *action_;
    };

    std::vector<StructInfo>   structs;
    std::vector<FunctionInfo> functions;

    auto cb = [&structs, &functions](std::vector<StructInfo> &&structs_, std::vector<FunctionInfo> &&functions_) {
        structs   = std::move(structs_);
        functions = std::move(functions_);
    };

    auto factoryPtr = std::make_unique<BindingGeneratorActionFactory>(cb);

    if (tool.run(factoryPtr.get()) != 0) {
        llvm::errs() << "Error running tool\n";
        return 1;
    }

    for (const auto &structInfo : structs) {
        llvm::outs() << "Struct: " << structInfo.name << "\n";
        for (const auto &[type, name] : structInfo.members) {
            llvm::outs() << "    " << type << " " << name << "\n";
        }
    }

    generateBindings(structs, functions, moduleName);

    return 0;
}