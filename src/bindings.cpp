#include "bindings.hpp"

#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/StringExtras.h>

#include <algorithm>
#include <vector>

namespace dd {

    namespace {

        using namespace clang::ast_matchers;

        // Binding name of the matched assignment expression.
        constexpr const char* AssignmentNode = "assignment";

        // The `object[key]` call that e is, if it is one (sol2 tables and proxies overload operator[]).
        const clang::CXXOperatorCallExpr* asSubscript(const clang::Expr* e) {
            const auto* call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(e->IgnoreImplicit());
            if (call == nullptr || call->getOperator() != clang::OO_Subscript || call->getNumArgs() != 2) {
                return nullptr;
            }
            return call;
        }

        // True for sol2 objects (state, state_view, table, proxy, usertype): plain maps are not Lua tables.
        bool isSolObject(clang::QualType type) {
            const clang::CXXRecordDecl* record = type.getNonReferenceType()->getAsCXXRecordDecl();
            return record != nullptr && llvm::StringRef(record->getQualifiedNameAsString()).starts_with("sol::");
        }

        // The code as written, for keys and roots that are not string literals.
        std::string sourceText(const clang::Expr* e, const MatchFinder::MatchResult& result) {
            const clang::CharSourceRange range = clang::CharSourceRange::getTokenRange(e->getSourceRange());
            return clang::Lexer::getSourceText(range, *result.SourceManager, result.Context->getLangOpts()).str();
        }

        // A key path segment: the literal's text, or `[source text]` for computed keys.
        std::string keyText(const clang::Expr* key, const MatchFinder::MatchResult& result) {
            if (const auto* literal = llvm::dyn_cast<clang::StringLiteral>(key->IgnoreImplicit())) {
                return literal->getString().str();
            }
            return "[" + sourceText(key, result) + "]";
        }

    } // namespace

    TableAssignmentCollector::TableAssignmentCollector(const SourceRoot& root) : root_(root) {}

    void TableAssignmentCollector::registerMatchers(MatchFinder& finder) {
        finder.addMatcher(
            cxxOperatorCallExpr(hasOverloadedOperatorName("="),
                                hasArgument(0, ignoringImplicit(cxxOperatorCallExpr(hasOverloadedOperatorName("[]")))))
                .bind(AssignmentNode),
            this);
    }

    void TableAssignmentCollector::run(const MatchFinder::MatchResult& result) {
        const auto* assignment = result.Nodes.getNodeAs<clang::CXXOperatorCallExpr>(AssignmentNode);
        std::vector<std::string> segments;
        const clang::Expr* object = assignment->getArg(0);
        while (const clang::CXXOperatorCallExpr* subscript = asSubscript(object)) {
            segments.push_back(keyText(subscript->getArg(1), result));
            object = subscript->getArg(0)->IgnoreImplicit();
        }
        if (!isSolObject(object->getType())) {
            return;
        }
        const std::optional<SourceLoc> loc = root_.locate(*result.SourceManager, assignment->getBeginLoc());
        if (!loc) {
            return;
        }
        segments.push_back(sourceText(object, result));
        std::ranges::reverse(segments);
        bindings_.insert({*loc, llvm::join(segments, ".")});
    }

} // namespace dd
