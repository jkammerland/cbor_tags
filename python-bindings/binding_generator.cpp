#include "ast_actions.hpp"
#include "include_tracker.hpp"
#include "visitor.hpp"

#include <fmt/format.h>
#include <fstream>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;

void generateBindings(const Structs &structs, const Functions &functions, const Headers &headers, const std::string &moduleName) {
    std::ofstream out(moduleName + ".cpp");

    // Write headers - TODO: be smart here, only include what is needed
    out << "#include <pybind11/pybind11.h>\n"
        << "#include <pybind11/stl.h>\n"
        << "#include <pybind11/complex.h>\n"
        << "#include <pybind11/functional.h>\n";

    // Include all headers
    out << "\n// User headers\n";
    for (const auto &header : headers) {
        if (!header.isSystem) {
            out << fmt::format("#include \"{}\"\n", header.fullPath);
        }
    }

    out << "\n// System headers\n";
    for (const auto &header : headers) {
        if (header.isSystem) {
            out << fmt::format("#include <{}>\n", header.name);
        }
    }

    out << "\nnamespace py = pybind11;\n\n";
    out << "PYBIND11_MODULE(" << moduleName << ", m) {\n";

    // Helper function to get fully qualified name
    auto getFullName = [](const StructInfo &s) { return s.name.qualified.empty() ? s.name.plain : s.name.qualified; };

    // First, declare all enums and classes
    for (const auto &structInfo : structs) {
        if (structInfo.isEnum) {
            out << fmt::format("    py::enum_<{0}>(m, \"{1}\", py::arithmetic())\n", getFullName(structInfo), structInfo.name.plain);
            for (const auto &member : structInfo.members) {
                out << fmt::format("        .value(\"{0}\", {1}::{0})\n", member.name.plain, getFullName(structInfo));
            }
            out << "        .export_values();\n\n";
        } else {
            out << fmt::format("    py::class_<{0}> {1}(m, \"{2}\");\n", getFullName(structInfo), structInfo.name.plain + "_class",
                               structInfo.name.plain);
        }
    }

    // Then, define the actual bindings for non-enum classes
    for (const auto &structInfo : structs) {
        if (structInfo.isEnum)
            continue;

        std::string className = structInfo.name.plain + "_class";
        std::string fullName  = getFullName(structInfo);

        // Main class definition
        out << fmt::format("    {0}\n", className) << "        .def(py::init<>())\n";

        // Add members
        for (const auto &member : structInfo.members) {
            out << fmt::format("        .def_readwrite(\"{0}\", &{1}::{0})\n", member.name.plain, fullName);
        }

        // Add member functions
        for (const auto &funcInfo : functions) {
            if (funcInfo.parent.has_value() && funcInfo.parent->qualified == fullName) {
                out << fmt::format("        .def(\"{0}\", &{1}::{0})\n", funcInfo.name.plain, fullName);
            }
        }
        // Remove last newline and add semicolon
        out.seekp(-1, std::ios_base::end);
        out << ";\n\n";
    }

    // Generate free function bindings
    for (const auto &funcInfo : functions) {
        if (funcInfo.isMemberFunction)
            continue;

        out << fmt::format("    m.def(\"{0}\", &{1});\n", funcInfo.name.plain,
                           funcInfo.name.qualified.empty() ? funcInfo.name.plain : funcInfo.name.qualified);
    }

    out << "}\n";
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        llvm::errs() << "Usage: " << argv[0] << "<module-name> <source-files>... [compiler-args...]\n";
        return 1;
    }

    // Print all args
    for (int i = 0; i < argc; ++i) {
        llvm::outs() << "argv[" << i << "] = " << argv[i] << "\n";
    }

    // Copy argv except for argv[1] (module name), but include argv[0] (program name)
    int          newArgc = argc - 1;
    const char **newArgv = new const char *[newArgc];
    newArgv[0]           = argv[0];
    for (int i = 2; i < argc; ++i) {
        newArgv[i - 1] = argv[i];
    }

    // print new args
    for (int i = 0; i < newArgc; ++i) {
        llvm::outs() << "newArgv[" << i << "] = " << newArgv[i] << "\n";
    }

    std::string moduleName = argv[1];

    auto expectedParser = clang::tooling::CommonOptionsParser::create(newArgc, newArgv, llvm::cl::getGeneralCategory());
    if (!expectedParser) {
        llvm::errs() << "Error parsing command line arguments: " << expectedParser.takeError() << "\n";
        return 1;
    }

    for (const auto &sourcePath : expectedParser->getSourcePathList()) {
        llvm::outs() << "Source path: " << sourcePath << "\n";
    }

    clang::tooling::ClangTool tool(expectedParser->getCompilations(), expectedParser->getSourcePathList());

    Structs   structs;
    Functions functions;
    Headers   headers;

    auto cb = [&structs, &functions](Structs &&structs_, Functions &&functions_) {
        structs.insert(structs.end(), std::make_move_iterator(structs_.begin()), std::make_move_iterator(structs_.end()));
        functions.insert(functions.end(), std::make_move_iterator(functions_.begin()), std::make_move_iterator(functions_.end()));
    };

    auto hcb = [&headers](Headers &&headers_) {
        headers.insert(headers.end(), std::make_move_iterator(headers_.begin()), std::make_move_iterator(headers_.end()));
    };

    auto factoryPtr = std::make_unique<DeclarationExtractionActionFactory>(cb, hcb);

    if (tool.run(factoryPtr.get()) != 0) {
        llvm::errs() << "Error running tool\n";
        return 1;
    }

    for (const auto &header : headers) {
        llvm::outs() << "Header: " << header.name << " system: <" << (header.isSystem ? "yes" : "no") << "> (" << header.fullPath << ")\n";
    }

    for (const auto &structInfo : structs) {
        llvm::outs() << "Struct: " << fmt::format("{0} ({1})", structInfo.name.plain, structInfo.name.qualified) << "\n";
        for (const auto &info : structInfo.members) {
            llvm::outs() << "    " << fmt::format("{0} {1}", info.type.plain, info.name.plain) << "\n";
        }
    }

    for (const auto &funcInfo : functions) {
        llvm::outs() << "Function: " << fmt::format("{0} ({1})", funcInfo.name.plain, funcInfo.name.qualified) << "\n";
        llvm::outs() << "    Return type: " << fmt::format("{0} ({1})", funcInfo.returnType.plain, funcInfo.returnType.qualified) << "\n";

        if (!funcInfo.parameters.empty()) {
            llvm::outs() << "    Parameters:\n";
        }
        for (const auto &info : funcInfo.parameters) {
            llvm::outs() << "    " << "    "
                         << fmt::format("{0} ({1}) {2} ({3})", info.type.plain, info.type.qualified, info.name.plain, info.name.qualified)
                         << "\n";
        }
    }

    generateBindings(structs, functions, headers, moduleName);

    return 0;
}