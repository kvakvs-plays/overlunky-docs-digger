#include "bindings.hpp"

#include "comments.hpp"
#include "resolve.hpp"
#include "types.hpp"
#include "usertypes.hpp"

#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <vector>

namespace dd {

    namespace {

        using namespace clang::ast_matchers;

        // Binding names of the matched nodes.
        constexpr const char* AssignmentNode = "assignment";
        constexpr const char* UsertypeNode = "usertype";
        constexpr const char* EnumNode = "enum";
        constexpr const char* LibrariesNode = "libraries";
        constexpr const char* FunctionNode = "function";
        constexpr const char* AliasNode = "alias";
        constexpr const char* ReadNode = "read";
        constexpr const char* TranslationUnitNode = "translation-unit";

        // n of an expression `1 << n` (parentheses and casts aside): a bit flag as its author wrote it.
        std::optional<int> shiftOfOne(const clang::Expr* expr, const clang::ASTContext& context) {
            const auto* shift = llvm::dyn_cast<clang::BinaryOperator>(expr->IgnoreParenCasts());
            if (shift == nullptr || shift->getOpcode() != clang::BO_Shl || shift->isValueDependent()) {
                return std::nullopt;
            }
            const std::optional<llvm::APSInt> one = shift->getLHS()->getIntegerConstantExpr(context);
            const std::optional<llvm::APSInt> amount = shift->getRHS()->getIntegerConstantExpr(context);
            if (!one || !amount || *one != 1 || amount->isNegative() || amount->uge(64)) {
                return std::nullopt;
            }
            return static_cast<int>(amount->getZExtValue());
        }

        // n of a value written as `1 << n`, or naming a C++ enumerator initialized so (`PAUSE_SCREEN::EXIT`).
        std::optional<int> writtenShift(const clang::Expr* value, const clang::ASTContext& context) {
            const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(value->IgnoreParenCasts());
            const auto* enumerator =
                reference == nullptr ? nullptr : llvm::dyn_cast<clang::EnumConstantDecl>(reference->getDecl());
            const clang::Expr* init = enumerator == nullptr ? nullptr : enumerator->getInitExpr();
            return shiftOfOne(init == nullptr ? value : init, context);
        }

        // An enum value: its text and, when the compiler knows it, its number or string.
        EnumValue enumValue(const clang::Expr* value, const clang::ASTContext& context) {
            EnumValue result;
            result.text = sourceText(value, context);
            result.shift = writtenShift(value, context);
            const clang::Expr* e = value->IgnoreImplicit();
            clang::Expr::EvalResult evaluated;
            if (!e->isValueDependent() && e->EvaluateAsInt(evaluated, context) &&
                evaluated.Val.getInt().isRepresentableByInt64()) {
                result.number = evaluated.Val.getInt().getExtValue();
            }
            else {
                result.string = stringValue(value);
            }
            return result;
        }

        // The doc of the local variable a binding passes on (`/// ...` above `sol::table meta_table{...};` for
        // `lua["meta"] = meta_table;`), or empty.
        Doc boundVariableDoc(const clang::Expr* value, const clang::ASTContext& context) {
            const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(withoutConversions(value));
            const auto* variable =
                reference == nullptr ? nullptr : llvm::dyn_cast<clang::VarDecl>(reference->getDecl());
            return variable != nullptr && variable->isLocalVarDecl() ? docBefore(variable->getBeginLoc(), context)
                                                                     : Doc{};
        }

        // The comment above an assignment, or else the comment of the local variable it binds.
        Doc bindingDoc(const clang::CXXOperatorCallExpr& assignment, const clang::ASTContext& context) {
            Doc doc = docBefore(assignment.getBeginLoc(), context);
            if (doc.lines.empty() && !doc.nodoc && !doc.deprecated) {
                doc = boundVariableDoc(assignment.getArg(1), context);
            }
            return doc;
        }

        // True for a doc that documents something: lines that are not hidden by NoDoc.
        bool documents(const Doc& doc) { return !doc.lines.empty() && !doc.nodoc; }

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
        finder.addMatcher(
            cxxMemberCallExpr(callee(cxxMethodDecl(hasAnyName("create_named_table", "new_enum")))).bind(EnumNode),
            this);
        finder.addMatcher(cxxMemberCallExpr(callee(cxxMethodDecl(hasName("open_libraries"))),
                                            forFunction(functionDecl().bind(FunctionNode)))
                              .bind(LibrariesNode),
                          this);
        finder.addMatcher(
            typedefNameDecl(hasDeclContext(anyOf(translationUnitDecl(), namespaceDecl()))).bind(AliasNode), this);
        finder.addMatcher(
            varDecl(hasInitializer(hasDescendant(cxxOperatorCallExpr(hasOverloadedOperatorName("[]"))))).bind(ReadNode),
            this);
        finder.addMatcher(translationUnitDecl().bind(TranslationUnitNode), this);
    }

    void TableAssignmentCollector::run(const MatchFinder::MatchResult& result) {
        const clang::ASTContext& context = *result.Context;
        if (const auto* assignment = result.Nodes.getNodeAs<clang::CXXOperatorCallExpr>(AssignmentNode)) {
            addAssignment(*assignment, result);
        }
        else if (const auto* usertype = result.Nodes.getNodeAs<clang::CXXMemberCallExpr>(UsertypeNode)) {
            addUsertype(*usertype, root_, context, out_);
        }
        else if (const auto* table = result.Nodes.getNodeAs<clang::CXXMemberCallExpr>(EnumNode)) {
            addEnum(*table, context);
        }
        else if (const auto* libraries = result.Nodes.getNodeAs<clang::CXXMemberCallExpr>(LibrariesNode)) {
            addLibraries(*libraries, *result.Nodes.getNodeAs<clang::FunctionDecl>(FunctionNode), context);
        }
        else if (const auto* alias = result.Nodes.getNodeAs<clang::TypedefNameDecl>(AliasNode)) {
            addAlias(*alias, context);
        }
        else if (const auto* read = result.Nodes.getNodeAs<clang::VarDecl>(ReadNode)) {
            addScriptCallback(*read, context);
        }
        else if (result.Nodes.getNodeAs<clang::TranslationUnitDecl>(TranslationUnitNode) != nullptr) {
            addCommentedBindings(context);
            addFiles(context.getSourceManager());
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
        if (!out_.functions.contains(key) && !out_.values.contains(key)) {
            addBinding(std::move(key), *path.root, assignment, context);
        }
    }

    void TableAssignmentCollector::addBinding(BindingKey key, const clang::Expr& rootExpr,
                                              const clang::CXXOperatorCallExpr& assignment,
                                              const clang::ASTContext& context) {
        const std::string root = isLuaState(rootExpr.getType()) ? std::string() : sourceText(&rootExpr, context);
        const clang::Expr* value = assignment.getArg(1);
        Doc doc = bindingDoc(assignment, context);
        if (doc.nodoc && commentedBindingAbove(assignment.getBeginLoc(), key.luaName, context)) {
            doc.nodoc = false;
        }
        Function function{key.owner, key.luaName, root, key.loc, doc, {}, false, false, {}, {}};
        if (describeFunction(function, value, context)) {
            out_.functions.emplace(std::move(key), std::move(function));
            return;
        }
        if (root.empty() && addEnumKey(key, value, doc, context)) {
            return;
        }
        // The bound expression's own type: definitionOf strips casts and enters factories, so a
        // `reinterpret_cast<T*>(offset + base)` would become the integer it casts.
        const clang::QualType type = value->IgnoreImplicit()->getType();
        out_.values.emplace(key, Value{key.owner, key.luaName, root, key.loc, doc, classifyType(type, context)});
    }

    bool TableAssignmentCollector::addEnumKey(const BindingKey& key, const clang::Expr* value, const Doc& doc,
                                              const clang::ASTContext& context) {
        const auto table = key.owner.size() == 1 ? out_.enums.find(key.owner.front()) : out_.enums.end();
        if (table == out_.enums.end()) {
            return false;
        }
        if (key.luaName.starts_with('[')) {
            table->second.filledAtRuntime = true; // a computed key: the code adds keys at runtime
            return true;
        }
        EnumValue entry = enumValue(value, context);
        entry.luaName = key.luaName;
        entry.doc = doc;
        entry.loc = key.loc;
        table->second.values.emplace(MemberKey{key.luaName, key.loc}, std::move(entry));
        return true;
    }

    void TableAssignmentCollector::addEnum(const clang::CXXMemberCallExpr& call, const clang::ASTContext& context) {
        const std::optional<SourceLoc> loc = root_.locate(context.getSourceManager(), call.getBeginLoc());
        const std::optional<std::string> name = call.getNumArgs() == 0 ? std::nullopt : stringValue(call.getArg(0));
        if (!loc || !name || !isSolObject(call.getImplicitObjectArgument()->getType())) {
            return;
        }
        Enum& table = out_.enums[*name];
        if (!table.loc.file.empty()) {
            return; // seen from another translation unit
        }
        table.luaName = *name;
        table.loc = *loc;
        table.doc = docBefore(call.getBeginLoc(), context);
        for (unsigned i = 1; i + 1 < call.getNumArgs(); i += 2) {
            const std::optional<SourceLoc> keyLoc =
                root_.locate(context.getSourceManager(), call.getArg(i)->getBeginLoc());
            EnumValue entry = enumValue(call.getArg(i + 1), context);
            entry.luaName = stringValue(call.getArg(i)).value_or(sourceText(call.getArg(i), context));
            entry.loc = keyLoc.value_or(*loc);
            table.values.emplace(MemberKey{entry.luaName, entry.loc}, std::move(entry));
        }
        addCommentedValues(table, call, context);
    }

    void TableAssignmentCollector::addCommentedValues(Enum& table, const clang::CXXMemberCallExpr& call,
                                                      const clang::ASTContext& context) const {
        for (const CommentedPair& pair : commentedPairs(call.getSourceRange(), context)) {
            EnumValue entry;
            entry.luaName = pair.luaName;
            entry.text = pair.text;
            entry.origin = EnumValueOrigin::DocComment;
            if (entry.luaName.empty()) {
                entry.doc.lines = {hintMarkdown(pair.text)};
            }
            entry.loc = root_.locate(context.getSourceManager(), pair.loc).value_or(table.loc);
            if (long long number = 0; !llvm::StringRef(pair.text).getAsInteger(0, number)) {
                entry.number = number;
            }
            table.values.emplace(MemberKey{entry.luaName, entry.loc}, std::move(entry));
        }
    }

    void TableAssignmentCollector::addLibraries(const clang::CXXMemberCallExpr& call,
                                                const clang::FunctionDecl& function, const clang::ASTContext& context) {
        const std::optional<SourceLoc> loc = root_.locate(context.getSourceManager(), call.getBeginLoc());
        if (!loc) {
            return;
        }
        LibraryCall libraries{{}, function.getNameAsString(), *loc};
        for (const clang::Expr* arg : call.arguments()) {
            if (std::optional<std::string> name = enumeratorName(arg)) {
                libraries.names.push_back(std::move(*name));
            }
        }
        out_.libraries.emplace(*loc, std::move(libraries));
    }

    void TableAssignmentCollector::addAlias(const clang::TypedefNameDecl& decl, const clang::ASTContext& context) {
        const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(&decl);
        const std::optional<SourceLoc> loc = root_.locate(context.getSourceManager(), decl.getLocation());
        if (!loc || out_.aliases.contains(decl.getNameAsString()) ||
            (alias != nullptr && alias->getDescribedAliasTemplate() != nullptr)) {
            return; // outside the root, seen before, or a template (no single aliased type)
        }
        out_.aliases.emplace(decl.getNameAsString(),
                             Alias{decl.getNameAsString(), classifyType(decl.getUnderlyingType(), context), *loc,
                                   docOfDecl(decl, context)});
    }

    void TableAssignmentCollector::addScriptCallback(const clang::VarDecl& variable, const clang::ASTContext& context) {
        const KeyPath path = keyPathOf(withoutConversions(variable.getInit()), context);
        if (path.keys.size() != 1 || path.keys.front().starts_with('[') || !isLuaState(path.root->getType())) {
            return;
        }
        const std::optional<SourceLoc> loc = root_.locate(context.getSourceManager(), variable.getBeginLoc());
        const Doc doc = docBefore(variable.getBeginLoc(), context);
        if (loc && documents(doc)) {
            const std::string& name = path.keys.front();
            out_.scriptCallbacks.emplace(MemberKey{name, *loc},
                                         ScriptCallback{name, classifyType(variable.getType(), context), *loc, doc});
        }
    }

    bool TableAssignmentCollector::addUsertypeMember(const KeyPath& path, const clang::CXXOperatorCallExpr& assignment,
                                                     const SourceLoc& loc, const clang::ASTContext& context) {
        const std::optional<clang::QualType> type = usertypeOf(path.root->getType());
        if (!type) {
            return false;
        }
        const clang::CXXRecordDecl* record = (*type)->getAsCXXRecordDecl();
        Usertype& usertype = out_.usertypes[usertypeKey(*type)];
        if (addBasesOrConstructors(usertype, {path.lastKey, assignment.getArg(1)}, record, root_, context)) {
            return true;
        }
        Member member = describeMember(assignment.getArg(1), record, root_, context);
        member.luaName = path.keys.front();
        member.doc = docBefore(assignment.getBeginLoc(), context);
        applyDocFallbacks(member, assignment.getBeginLoc(), record, context);
        member.loc = loc;
        usertype.members.emplace(MemberKey{member.luaName, loc}, std::move(member));
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
        if (llvm::StringRef(binding.signature).starts_with('[')) {
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

    void TableAssignmentCollector::addFiles(const clang::SourceManager& sm) {
        for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
            const llvm::StringRef real = it->first.getFileEntry().tryGetRealPathName();
            llvm::SmallString<256> path(real.empty() ? it->first.getName() : real);
            const std::error_code error = llvm::sys::fs::make_absolute(path);
            files_.push_back(error ? it->first.getName().str() : path.str().str());
        }
        std::ranges::sort(files_);
        files_.erase(std::unique(files_.begin(), files_.end()), files_.end());
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
            std::ranges::move(blockDocs(file, sm), std::back_inserter(out_.blockDocs));
        }
    }

} // namespace dd
