#pragma once

#include "model.hpp"
#include "source_root.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>

#include <compare>
#include <set>
#include <string>

namespace dd {

    // One `root["a"]["b"] = value` statement on a sol2 object: the starting point of every Lua API binding (B1).
    struct KeyPathBinding {
        // Where the assignment is written.
        SourceLoc loc;
        // Root expression and keys joined with '.'; a key that is not a string literal is kept as `[source text]`.
        std::string path;

        auto operator<=>(const KeyPathBinding&) const = default;
    };

    // Collects table assignments (pass B1) from every translation unit run through the registered matchers.
    // Results are kept once even when a header is seen from several translation units.
    class TableAssignmentCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
    public:
        // Only assignments located under root are kept.
        explicit TableAssignmentCollector(const SourceRoot& root);

        // Adds the B1 matcher with this collector as its callback.
        void registerMatchers(clang::ast_matchers::MatchFinder& finder);

        // Records one matched assignment.
        void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;

        // All assignments found so far, ordered by location.
        const std::set<KeyPathBinding>& bindings() const { return bindings_; }

    private:
        // Decides which files results may come from and how their paths are written.
        const SourceRoot& root_;
        // Found assignments; a set, because headers and template instantiations repeat them.
        std::set<KeyPathBinding> bindings_;
    };

} // namespace dd
