#pragma once

#include "comments.hpp"
#include "model.hpp"
#include "resolve.hpp"
#include "source_root.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>

#include <set>
#include <string>

namespace dd {

    // Passes B1 and B2: `root["a"]["b"] = value` statements on sol2 objects become Lua functions, values or
    // usertype members, `new_usertype<T>(...)` calls become usertypes, and commented-out bindings below a `///`
    // block become doc-only entries. Results from all translation units go into one Extraction; a binding seen
    // again (headers, template instantiations) is kept once.
    class TableAssignmentCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
    public:
        // Only bindings located under root are kept.
        TableAssignmentCollector(const SourceRoot& root, Extraction& out);

        // Adds the B1 and B2 matchers with this collector as their callback.
        void registerMatchers(clang::ast_matchers::MatchFinder& finder);

        // Records one matched assignment or usertype, or scans a translation unit's files for commented-out
        // bindings.
        void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;

    private:
        // Turns one `root[...] = value` into a Function or a Value.
        void addAssignment(const clang::CXXOperatorCallExpr& assignment,
                           const clang::ast_matchers::MatchFinder::MatchResult& result);
        // `ut["k"] = v` on a sol::usertype<T> object: a member of T's usertype. False for other roots.
        bool addUsertypeMember(const KeyPath& path, const clang::CXXOperatorCallExpr& assignment, const SourceLoc& loc,
                               const clang::ASTContext& context);
        // Fills function from value when value is callable, Lua code, or another Lua function; false for values.
        bool describeFunction(Function& function, const clang::Expr* value, const clang::ASTContext& context) const;
        // Adds the commented-out bindings of every file under the root not scanned before.
        void addCommentedBindings(const clang::ASTContext& context);
        // True the first time file (a file under the root) is seen: each file is scanned once over all TUs.
        bool startScan(clang::FileID file, const clang::SourceManager& sm);
        // Adds one commented-out binding as a doc-only function (lambda text) or value (anything else).
        void addCommentedBinding(const CommentedBinding& binding, const SourceLoc& loc);

        // Decides which files results may come from and how their paths are written.
        const SourceRoot& root_;
        // Where results go.
        Extraction& out_;
        // Root-relative files already scanned for commented-out bindings (every TU sees the shared headers).
        std::set<std::string> scannedFiles_;
    };

    // Copies the overloads of aliased functions into their aliases, once every translation unit is parsed (the
    // alias and its target may be bound in different files).
    void resolveAliases(Extraction& extraction);

} // namespace dd
