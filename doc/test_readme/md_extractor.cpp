#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class MarkdownCodeExtractor {
  public:
    struct Config {
        std::vector<fs::path> inputPaths;
        fs::path              outputDir;
        bool                  verbose = false;
    };

    explicit MarkdownCodeExtractor(const Config &config) : config(config) {
        validateConfig();
        prepareOutputDirectory();
    }

    void extractAll() {
        for (const auto &path : config.inputPaths) {
            if (config.verbose) {
                std::cout << "Processing: " << path << std::endl;
            }
            extractFile(path);
        }
    }

  private:
    Config config;

    void validateConfig() {
        // Validate input paths
        for (const auto &path : config.inputPaths) {
            if (!fs::exists(path)) {
                throw std::runtime_error("Input file does not exist: " + path.string());
            }
            if (path.extension() != ".md") {
                throw std::runtime_error("Input file is not a markdown file: " + path.string());
            }
        }
    }

    void prepareOutputDirectory() {
        if (config.outputDir.empty()) {
            config.outputDir = fs::current_path() / "extracted_code";
        }

        if (!fs::exists(config.outputDir)) {
            fs::create_directories(config.outputDir);
        }
    }

    void extractFile(const fs::path &mdPath) {
        std::ifstream inFile(mdPath);
        if (!inFile) {
            throw std::runtime_error("Cannot open input file: " + mdPath.string());
        }

        std::string line;
        bool        inCodeBlock    = false;
        int         codeBlockCount = 0;
        std::string currentCode;
        std::string language;

        while (std::getline(inFile, line)) {
            if (line.starts_with("```")) {
                if (!inCodeBlock) {
                    // Start of code block
                    language = line.substr(3);
                    if (language == "cpp" || language == "c++" || language == "cxx") {
                        inCodeBlock = true;
                        currentCode.clear();
                    }
                } else {
                    // End of code block
                    inCodeBlock = false;
                    if (!currentCode.empty()) {
                        saveCodeToFile(currentCode, mdPath, ++codeBlockCount);
                    }
                }
            } else if (inCodeBlock) {
                currentCode += line + "\n";
            }
        }
    }

    void saveCodeToFile(const std::string &code, const fs::path &sourcePath, int blockNumber) {
        std::string filename = generateFilename(sourcePath, blockNumber);
        fs::path    fullPath = config.outputDir / filename;

        std::ofstream outFile(fullPath);
        if (!outFile) {
            std::cerr << "Failed to create output file: " << fullPath << std::endl;
            return;
        }

        outFile << code;
        if (config.verbose) {
            std::cout << "Created file: " << fullPath << std::endl;
        }
    }

    std::string generateFilename(const fs::path &sourcePath, int blockNumber) {
        std::string baseName = sourcePath.stem().string();
        return baseName + "_example_" + std::to_string(blockNumber) + ".cpp";
    }
};

void printUsage(const char *programName) {
    std::cout << "Usage: " << programName << " [options] <input_markdown_files...>\n"
              << "Options:\n"
              << "  -o, --output <dir>   Specify output directory (default: ./extracted_code)\n"
              << "  -v, --verbose        Enable verbose output\n"
              << "  -h, --help           Show this help message\n";
}

int main(int argc, char *argv[]) {
    try {
        if (argc < 2) {
            printUsage(argv[0]);
            return 1;
        }

        MarkdownCodeExtractor::Config config;

        // Parse command line arguments
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                printUsage(argv[0]);
                return 0;
            } else if (arg == "-o" || arg == "--output") {
                if (++i >= argc) {
                    throw std::runtime_error("Output directory not specified");
                }
                config.outputDir = argv[i];
            } else if (arg == "-v" || arg == "--verbose") {
                config.verbose = true;
            } else {
                config.inputPaths.push_back(arg);
            }
        }

        if (config.inputPaths.empty()) {
            throw std::runtime_error("No input files specified");
        }

        MarkdownCodeExtractor extractor(config);
        extractor.extractAll();

        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}