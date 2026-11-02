#pragma once

#include "model.hpp"

#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/StringRef.h>

#include <optional>
#include <string>

namespace dd {

    // Limits results to one source tree and makes their paths relative to it, so the same input gives the same
    // output on every machine and checkout location.
    class SourceRoot {
    public:
        // Canonicalizes directory; it does not have to exist (nothing is then located inside it).
        explicit SourceRoot(llvm::StringRef directory);

        // The file location of loc relative to the root, or nothing when it lies outside the root.
        std::optional<SourceLoc> locate(const clang::SourceManager& sm, clang::SourceLocation loc) const;

    private:
        // Canonical absolute root path with '/' separators and a trailing '/'.
        std::string prefix_;
    };

} // namespace dd
