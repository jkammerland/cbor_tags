#include "ast_actions.hpp"

#include <fmt/format.h>
#include <fstream>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;

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

    for (const auto &sourcePath : expectedParser->getSourcePathList()) {
        llvm::outs() << "Source path: " << sourcePath << "\n";
    }

    clang::tooling::ClangTool tool(expectedParser->getCompilations(), expectedParser->getSourcePathList());

    std::vector<StructInfo>   structs;
    std::vector<FunctionInfo> functions;
    Headers                   headers;

    auto cb = [&structs, &functions](std::vector<StructInfo> &&structs_, std::vector<FunctionInfo> &&functions_) {
        structs   = std::move(structs_);
        functions = std::move(functions_);
    };

    auto hcb = [&headers](Headers &&headers_) { headers = std::move(headers_); };

    auto factoryPtr = std::make_unique<DeclarationExtractionActionFactory>(cb, hcb);

    if (tool.run(factoryPtr.get()) != 0) {
        llvm::errs() << "Error running tool\n";
        return 1;
    }

    for (const auto &header : headers) {
        llvm::outs() << "Header: " << header.name << " system: <" << (header.isSystem ? "yes" : "no") << "> (" << header.fullPath << ")\n";
    }

    for (const auto &structInfo : structs) {
        llvm::outs() << "Struct: " << structInfo.name << "\n";
        for (const auto &[type, name] : structInfo.members) {
            llvm::outs() << "    " << type << " " << name << "\n";
        }
    }

    for (const auto &funcInfo : functions) {
        llvm::outs() << "Function: " << funcInfo.name << "\n";
        llvm::outs() << "    Return type: " << funcInfo.returnType << "\n";

        if (!funcInfo.parameters.empty()) {
            llvm::outs() << "    Parameters:\n";
        }
        for (const auto &[type, name] : funcInfo.parameters) {
            llvm::outs() << "    " << "    " << type << " " << name << "\n";
        }
    }

    // generateBindings(structs, functions, moduleName);

    return 0;
}