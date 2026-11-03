#pragma once

#include "model.hpp"
#include "source_root.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

// Passes C2 to C6 and the AST part of C5: what the code does with the Lua functions it receives. Facts go into
// CallbackFacts and are joined with the bound signatures once every translation unit is parsed (merge.hpp).
namespace dd {

    // Collects, per translation unit: the flow of every sol::function parameter of a defined function (make_safe_cb,
    // handle_function, direct calls, storage in a record field, forwarding), the handle_function calls on stored
    // callbacks with the enum key that selects them, the enum arguments of calls (to follow keys back to their
    // constants), container writes under an enum guard, and the vtable hooks of HookableVTable variables.
    class CallbackCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
    public:
        // Only code under root is reported.
        CallbackCollector(const SourceRoot& root, CallbackFacts& out);

        // Adds the matchers with this collector as their callback.
        void registerMatchers(clang::ast_matchers::MatchFinder& finder);

        // Records the facts of one matched node.
        void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;

    private:
        // Records the flow of each sol::function parameter of function.
        void addFlows(const clang::FunctionDecl& function, clang::ASTContext& context);
        // Records a handle_function call on a stored callback, in function.
        void addSite(const clang::CallExpr& call, const clang::FunctionDecl& function, clang::ASTContext& context);
        // Records the enum argument passed to param.
        void addEdge(const clang::Expr& argument, const clang::ParmVarDecl& param, const clang::CallExpr& call,
                     const clang::ASTContext& context);
        // Records `container[k] = v` under an enum constant guard on v.
        void addContainerKey(const clang::CXXOperatorCallExpr& assignment, clang::ASTContext& context);
        // Records the hooks a HookableVTable variable registers on its usertype.
        void addVtable(const clang::VarDecl& variable, clang::ASTContext& context);

        // Decides which code is reported.
        const SourceRoot& root_;
        // Where facts go.
        CallbackFacts& out_;
    };

} // namespace dd
