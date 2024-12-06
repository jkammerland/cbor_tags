#pragma once

#include <clang/Basic/SourceManager.h>
#include <clang/Lex/PPCallbacks.h>
#include <utility>

struct Header {
    std::string name;
    std::string fullPath;
    bool        isSystem;
    bool        isInputFile;
};

using Headers = std::vector<Header>;

using HeaderCallback = std::function<void(Headers &&)>;

class IncludeTracker : public clang::PPCallbacks {
  public:
    explicit IncludeTracker(clang::SourceManager &SM, HeaderCallback cb) : sourceManager_(SM), cb_(std::move(cb)) {}
    ~IncludeTracker() override { cb_(std::move(headers_)); }

    void InclusionDirective(clang::SourceLocation HashLoc, const clang::Token &IncludeTok, llvm::StringRef FileName, bool IsAngled,
                            clang::CharSourceRange FilenameRange, clang::OptionalFileEntryRef File, llvm::StringRef SearchPath,
                            llvm::StringRef RelativePath, const clang::Module *SuggestedModule, bool ModuleImported,
                            clang::SrcMgr::CharacteristicKind FileType) override {

        // Get the location of the include directive
        auto includeLoc = HashLoc;

        // Check if this is a direct include from our main file
        bool isDirectInclude = sourceManager_.getFileID(includeLoc) == sourceManager_.getMainFileID();

        // Get the full path of the included file
        std::string fullPath;
        if (File) {
            fullPath = File->getFileEntry().tryGetRealPathName().str();
        }

        // Only process direct includes from the main file
        if (isDirectInclude) {
            headers_.push_back({FileName.str(), fullPath, FileType != clang::SrcMgr::C_User, false});

            // llvm::outs() << "Kind: " << static_cast<int>(FileType) << " " << FileName << " (from main file)\n";
            // llvm::outs() << "Direct include: " << FileName << " (from main file)\n";
            // if (!fullPath.empty()) {
            //     llvm::outs() << "Full path: " << fullPath << "\n";
            // }
        }
    }

  private:
    const clang::SourceManager &sourceManager_;

    Headers        headers_;
    HeaderCallback cb_;
};