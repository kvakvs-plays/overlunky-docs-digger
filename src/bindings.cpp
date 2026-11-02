#include "bindings.hpp"

#include "comments.hpp"
#include "resolve.hpp"
#include "types.hpp"
#include "usertypes.hpp"

#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>

#include <map>
#include <vector>

namespace dd {

    namespace {

        using namespace clang::ast_matchers;

        // Binding names of the matched nodes.
        constexpr const char* AssignmentNode = "assignment";
        constexpr const char* UsertypeNode = "usertype";
        constexpr const char* TranslationUnitNode = "translation-unit";

        // The doc-only Function for a commented-out binding.
        Function functionFromComment(const CommentedBinding& binding, const SourceLoc& loc) {
            Function function;
            function.luaName = binding.luaName;
            function.loc = loc;
            function.doc = binding.doc;
            function.overloads.push_back(signatureFromText(binding.signature));
            function.docOnly = true;
            return function;
        }

    } // namespace

    TableAssignmentCollector::TableAssignmentCollector(const SourceRoot& root, Extraction& out) :
        root_(root), out_(out) {}

    void TableAssignmentCollector::registerMatchers(MatchFinder& finder) {
        finder.addMatcher(
            cxxOperatorCallExpr(hasOverloadedOperatorName("="),
                                hasArgument(0, ignoringImplicit(cxxOperatorCallExpr(hasOverloadedOperatorName("[]")))))
                .bind(AssignmentNode),
            this);
        finder.addMatcher(cxxMemberCallExpr(callee(cxxMethodDecl(hasName("new_usertype")))).bind(UsertypeNode), this);
        finder.addMatcher(translationUnitDecl().bind(TranslationUnitNode), this);
    }

    void TableAssignmentCollector::run(const MatchFinder::MatchResult& result) {
        if (const auto* assignment = result.Nodes.getNodeAs<clang::CXXOperatorCallExpr>(AssignmentNode)) {
            addAssignment(*assignment, result);
        }
        else if (const auto* usertype = result.Nodes.getNodeAs<clang::CXXMemberCallExpr>(UsertypeNode)) {
            addUsertype(*usertype, root_, *result.Context, out_);
        }
        else if (result.Nodes.getNodeAs<clang::TranslationUnitDecl>(TranslationUnitNode) != nullptr) {
            addCommentedBindings(*result.Context);
        }
    }

    void TableAssignmentCollector::addAssignment(const clang::CXXOperatorCallExpr& assignment,
                                                 const MatchFinder::MatchResult& result) {
        const clang::ASTContext& context = *result.Context;
        KeyPath path = keyPathOf(assignment.getArg(0), context);
        if (!isSolObject(path.root->getType())) {
            return;
        }
        const std::optional<SourceLoc> loc = root_.locate(context.getSourceManager(), assignment.getBeginLoc());
        if (!loc) {
            return;
        }
        if (path.keys.size() == 1 && addUsertypeMember(path, assignment, *loc, context)) {
            return;
        }
        std::string name = std::move(path.keys.back());
        path.keys.pop_back();
        BindingKey key{std::move(path.keys), std::move(name), *loc};
        if (out_.functions.contains(key) || out_.values.contains(key)) {
            return;
        }
        const std::string root = isLuaState(path.root->getType()) ? std::string() : sourceText(path.root, context);
        const Doc doc = docBefore(assignment.getBeginLoc(), context);

        Function function{key.owner, key.luaName, root, key.loc, doc, {}, false, false, {}};
        const clang::Expr* value = assignment.getArg(1);
        if (!describeFunction(function, value, context)) {
            const clang::QualType type = definitionOf(value).expr->getType();
            out_.values.emplace(key, Value{key.owner, key.luaName, root, key.loc, doc, classifyType(type, context)});
            return;
        }
        out_.functions.emplace(std::move(key), std::move(function));
    }

    bool TableAssignmentCollector::addUsertypeMember(const KeyPath& path, const clang::CXXOperatorCallExpr& assignment,
                                                     const SourceLoc& loc, const clang::ASTContext& context) {
        const std::optional<clang::QualType> type = usertypeOf(path.root->getType());
        if (!type) {
            return false;
        }
        const clang::CXXRecordDecl* record = (*type)->getAsCXXRecordDecl();
        Member member = describeMember(assignment.getArg(1), record, root_, context);
        member.luaName = path.keys.front();
        applyDocFallbacks(member, assignment.getBeginLoc(), record, context);
        member.loc = loc;
        member.doc = docBefore(assignment.getBeginLoc(), context);
        out_.usertypes[usertypeKey(*type)].members.emplace(MemberKey{member.luaName, loc}, std::move(member));
        return true;
    }

    bool TableAssignmentCollector::describeFunction(Function& function, const clang::Expr* value,
                                                    const clang::ASTContext& context) const {
        const std::vector<BoundFunction> targets = boundFunctions(value);
        for (const BoundFunction& target : targets) {
            function.overloads.push_back(signatureOf(target, root_, context));
        }
        if (!targets.empty()) {
            return true;
        }
        if (isScriptResult(value)) {
            function.definedInLua = true;
            return true;
        }
        KeyPath aliased = keyPathOf(value, context);
        if (aliased.keys.empty() || !isLuaState(aliased.root->getType())) {
            return false;
        }
        function.aliasOf = std::move(aliased.keys);
        return true;
    }

    void resolveAliases(Extraction& extraction) {
        std::map<std::vector<std::string>, const Function*> byPath;
        for (const auto& [key, function] : extraction.functions) {
            // Only compiled bindings are targets: a commented-out signature of the same name has no types.
            if (!function.docOnly) {
                std::vector<std::string> path = key.owner;
                path.push_back(key.luaName);
                byPath.emplace(std::move(path), &function);
            }
        }
        for (auto& [key, function] : extraction.functions) {
            const auto target = byPath.find(function.aliasOf);
            if (!function.aliasOf.empty() && target != byPath.end() && target->second->aliasOf.empty()) {
                function.overloads = target->second->overloads;
            }
        }
    }

    void TableAssignmentCollector::addCommentedBinding(const CommentedBinding& binding, const SourceLoc& loc) {
        BindingKey key{{}, binding.luaName, loc};
        // A lambda text stands for a function; anything else (`lua.create_named_table(...)`) for a value.
        if (llvm::StringRef(binding.signature).starts_with("[")) {
            out_.functions.emplace(std::move(key), functionFromComment(binding, loc));
            return;
        }
        Value value{{}, binding.luaName, {}, loc, binding.doc, {}, true};
        value.type.spelling = binding.signature;
        out_.values.emplace(std::move(key), std::move(value));
    }

    bool TableAssignmentCollector::startScan(clang::FileID file, const clang::SourceManager& sm) {
        const std::optional<SourceLoc> start =
            file.isValid() ? root_.locate(sm, sm.getLocForStartOfFile(file)) : std::nullopt;
        return start && scannedFiles_.insert(start->file).second;
    }

    void TableAssignmentCollector::addCommentedBindings(const clang::ASTContext& context) {
        const clang::SourceManager& sm = context.getSourceManager();
        for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
            const clang::FileID file = sm.translateFile(it->first);
            if (!startScan(file, sm)) {
                continue;
            }
            for (const CommentedBinding& binding : commentedBindings(file, context)) {
                if (const std::optional<SourceLoc> loc = root_.locate(sm, binding.loc)) {
                    addCommentedBinding(binding, *loc);
                }
            }
        }
    }

} // namespace dd
