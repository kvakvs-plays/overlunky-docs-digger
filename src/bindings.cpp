#include "bindings.hpp"

#include "comments.hpp"
#include "types.hpp"

#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Lex/Lexer.h>

#include <algorithm>
#include <map>
#include <vector>

namespace dd {

    namespace {

        using namespace clang::ast_matchers;

        // Binding names of the matched nodes.
        constexpr const char* AssignmentNode = "assignment";
        constexpr const char* TranslationUnitNode = "translation-unit";

        // The keys of `root["a"]["b"]` and the root expression.
        struct KeyPath {
            // Keys from the root outwards; a key that is not a string literal is kept as `[source text]`.
            std::vector<std::string> keys;
            // The object the first operator[] is applied to.
            const clang::Expr* root = nullptr;
        };

        // The `object[key]` call that e is, if it is one (sol2 tables and proxies overload operator[]).
        const clang::CXXOperatorCallExpr* asSubscript(const clang::Expr* e) {
            const auto* call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(e->IgnoreImplicit());
            if (call == nullptr || call->getOperator() != clang::OO_Subscript || call->getNumArgs() != 2) {
                return nullptr;
            }
            return call;
        }

        // Qualified name of the class of type, or empty for non-class types.
        std::string recordName(clang::QualType type) {
            const clang::CXXRecordDecl* record = type.getNonReferenceType()->getAsCXXRecordDecl();
            return record == nullptr ? std::string() : record->getQualifiedNameAsString();
        }

        // True for sol2 objects (state, state_view, table, proxy, usertype): plain maps are not Lua tables.
        bool isSolObject(clang::QualType type) { return llvm::StringRef(recordName(type)).starts_with("sol::"); }

        // True when the root is the Lua state or a script's environment (whose keys are that script's globals), so
        // the keys name globals.
        bool isLuaState(clang::QualType type) {
            const std::string name = recordName(type);
            return name == "sol::state" || name == "sol::state_view" || name == "sol::basic_environment";
        }

        // The code as written, for keys and roots that are not string literals.
        std::string sourceText(const clang::Expr* e, const clang::ASTContext& context) {
            const clang::CharSourceRange range = clang::CharSourceRange::getTokenRange(e->getSourceRange());
            return clang::Lexer::getSourceText(range, context.getSourceManager(), context.getLangOpts()).str();
        }

        // A key path segment: the literal's text, or `[source text]` for computed keys.
        std::string keyText(const clang::Expr* key, const clang::ASTContext& context) {
            if (const auto* literal = llvm::dyn_cast<clang::StringLiteral>(key->IgnoreImplicit())) {
                return literal->getString().str();
            }
            return "[" + sourceText(key, context) + "]";
        }

        // Splits the left side of an assignment into its keys and root.
        KeyPath keyPathOf(const clang::Expr* target, const clang::ASTContext& context) {
            KeyPath path;
            const clang::Expr* object = target;
            while (const clang::CXXOperatorCallExpr* subscript = asSubscript(object)) {
                path.keys.push_back(keyText(subscript->getArg(1), context));
                object = subscript->getArg(0)->IgnoreImplicit();
            }
            std::ranges::reverse(path.keys);
            path.root = object;
            return path;
        }

        // Qualified name of the function a call goes to, or empty.
        std::string calleeName(const clang::CallExpr& call) {
            const clang::FunctionDecl* callee = call.getDirectCallee();
            return callee == nullptr ? std::string() : callee->getQualifiedNameAsString();
        }

        void addBound(const clang::Expr* e, std::vector<const clang::FunctionDecl*>& out);

        // A function name binds that function; a local variable binds what it was initialized with
        // (`auto spawn_tree = sol::overload(...); lua["spawn_tree"] = spawn_tree;`).
        void addBoundReference(const clang::DeclRefExpr& reference, std::vector<const clang::FunctionDecl*>& out) {
            if (const auto* function = llvm::dyn_cast<clang::FunctionDecl>(reference.getDecl())) {
                out.push_back(function);
            }
            else if (const auto* variable = llvm::dyn_cast<clang::VarDecl>(reference.getDecl())) {
                if (variable->isLocalVarDecl() && variable->getInit() != nullptr) {
                    addBound(variable->getInit(), out);
                }
            }
        }

        // `lua.safe_script(...)` / `lua.script(...)`: a value computed by Lua code, here a function written in Lua.
        bool isScriptResult(const clang::Expr* e) {
            const auto* call = llvm::dyn_cast<clang::CXXMemberCallExpr>(e->IgnoreImplicit());
            const clang::CXXMethodDecl* method = call == nullptr ? nullptr : call->getMethodDecl();
            return method != nullptr && (method->getName() == "safe_script" || method->getName() == "script");
        }

        // sol::overload(a, b, ...) binds each argument; sol::resolve<Sig>(f) binds the overload Clang picked.
        void addBoundCall(const clang::CallExpr& call, std::vector<const clang::FunctionDecl*>& out) {
            const std::string name = calleeName(call);
            if (name == "sol::overload") {
                for (const clang::Expr* argument : call.arguments()) {
                    addBound(argument, out);
                }
            }
            else if (name == "sol::resolve" && call.getNumArgs() == 1) {
                addBound(call.getArg(0), out);
            }
        }

        // resolveBound (plan B1): the C++ functions value binds; none when value is not callable.
        void addBound(const clang::Expr* e, std::vector<const clang::FunctionDecl*>& out) {
            const clang::Expr* value = e->IgnoreImplicit()->IgnoreParenCasts();
            if (const auto* lambda = llvm::dyn_cast<clang::LambdaExpr>(value)) {
                out.push_back(lambda->getCallOperator());
            }
            else if (const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(value)) {
                addBoundReference(*reference, out);
            }
            else if (const auto* address = llvm::dyn_cast<clang::UnaryOperator>(value)) {
                if (address->getOpcode() == clang::UO_AddrOf) {
                    addBound(address->getSubExpr(), out);
                }
            }
            else if (const auto* call = llvm::dyn_cast<clang::CallExpr>(value)) {
                addBoundCall(*call, out);
            }
        }

        // Marks the trailing run of nullable Lua-visible parameters: sol2 passes nil for arguments left out.
        void markOmittable(std::vector<Param>& params) {
            for (auto it = params.rbegin(); it != params.rend(); ++it) {
                if (it->type.hidden) {
                    continue;
                }
                if (!it->type.nullable) {
                    return;
                }
                it->omittable = true;
            }
        }

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
        finder.addMatcher(translationUnitDecl().bind(TranslationUnitNode), this);
    }

    void TableAssignmentCollector::run(const MatchFinder::MatchResult& result) {
        if (const auto* assignment = result.Nodes.getNodeAs<clang::CXXOperatorCallExpr>(AssignmentNode)) {
            addAssignment(*assignment, result);
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
            const clang::QualType type = value->IgnoreImplicit()->getType();
            out_.values.emplace(key, Value{key.owner, key.luaName, root, key.loc, doc, classifyType(type, context)});
            return;
        }
        out_.functions.emplace(std::move(key), std::move(function));
    }

    bool TableAssignmentCollector::describeFunction(Function& function, const clang::Expr* value,
                                                    const clang::ASTContext& context) const {
        std::vector<const clang::FunctionDecl*> targets;
        addBound(value, targets);
        for (const clang::FunctionDecl* target : targets) {
            function.overloads.push_back(signatureOf(*target, context));
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

    FunctionSig TableAssignmentCollector::signatureOf(const clang::FunctionDecl& function,
                                                      const clang::ASTContext& context) const {
        FunctionSig sig;
        for (const clang::ParmVarDecl* param : function.parameters()) {
            sig.params.push_back({param->getNameAsString(), classifyType(param->getType(), context), false});
        }
        markOmittable(sig.params);
        sig.returns = classifyReturns(function.getReturnType(), context);
        const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
        if (method != nullptr && method->getParent()->isLambda()) {
            const std::optional<SourceLoc> loc = root_.locate(context.getSourceManager(), function.getLocation());
            sig.cppTarget = loc ? "lambda at " + loc->file + ":" + std::to_string(loc->line) : "lambda";
            return sig;
        }
        sig.cppTarget = function.getQualifiedNameAsString();
        sig.doc = docOfDecl(function, context);
        return sig;
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
