#include "source_root.hpp"

#include <clang/Basic/FileManager.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

#include <algorithm>

namespace dd {

    namespace {

        // Same spelling for a path however it was written: forward slashes only.
        std::string generic(llvm::StringRef path) {
            std::string result = path.str();
            std::ranges::replace(result, '\\', '/');
            return result;
        }

        // Path prefix test that follows the host file system: Windows paths compare without case.
        bool startsWithPath(llvm::StringRef path, llvm::StringRef prefix) {
            if (llvm::sys::path::is_style_windows(llvm::sys::path::Style::native)) {
                return path.starts_with_insensitive(prefix);
            }
            return path.starts_with(prefix);
        }

    } // namespace

    SourceRoot::SourceRoot(llvm::StringRef directory) {
        llvm::SmallString<256> canonical;
        if (llvm::sys::fs::real_path(directory, canonical)) {
            // A missing directory: keep it as given (absolute when possible); nothing will be located inside it.
            canonical = directory;
            if (!llvm::sys::fs::make_absolute(canonical)) {
                llvm::sys::path::remove_dots(canonical, true);
            }
        }
        prefix_ = generic(canonical);
        if (!llvm::StringRef(prefix_).ends_with("/")) {
            prefix_ += '/';
        }
    }

    std::optional<SourceLoc> SourceRoot::locate(const clang::SourceManager& sm, clang::SourceLocation loc) const {
        const clang::SourceLocation fileLoc = sm.getFileLoc(loc);
        const clang::OptionalFileEntryRef entry = sm.getFileEntryRefForID(sm.getFileID(fileLoc));
        if (!entry) {
            return std::nullopt;
        }
        const std::string path = generic(sm.getFileManager().getCanonicalName(*entry));
        if (!startsWithPath(path, prefix_)) {
            return std::nullopt;
        }
        return SourceLoc{path.substr(prefix_.size()), sm.getExpansionLineNumber(fileLoc),
                         sm.getExpansionColumnNumber(fileLoc)};
    }

} // namespace dd
