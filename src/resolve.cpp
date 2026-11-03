#include "resolve.hpp"

#include "comments.hpp"
#include "types.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Lex/Lexer.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>

namespace dd {

    namespace {

        // How many pass-through steps definitionOf follows; real chains have two or three.
        constexpr int MaxDefinitionSteps = 8;

        // The `object[key]` call that e is, if it is one (sol2 tables and proxies overload operator[]).
        const clang::CXXOperatorCallExpr* asSubscript(const clang::Expr* e) {
            const auto* call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(e->IgnoreImplicit());
            if (call == nullptr || call->getOperator() != clang::OO_Subscript || call->getNumArgs() != 2) {
                return nullptr;
            }
            return call;
        }

        // A key path segment: the literal's text, or `[source text]` for computed keys.
        std::string keyText(const clang::Expr* key, const clang::ASTContext& context) {
            if (const auto* literal = llvm::dyn_cast<clang::StringLiteral>(key->IgnoreImplicit())) {
                return literal->getString().str();
            }
            return "[" + sourceText(key, context) + "]";
        }

        // Implicit nodes, parentheses and casts removed: what the code names.
        const clang::Expr* stripped(const clang::Expr* e) { return e->IgnoreImplicit()->IgnoreParenCasts(); }

        // True for a statement that only declares types or checks (`using T = ...;`, `static_assert(...)`).
        bool declaresNoVariable(const clang::Stmt* statement) {
            const auto* declaration = llvm::dyn_cast<clang::DeclStmt>(statement);
            return declaration != nullptr && std::ranges::none_of(declaration->decls(), [](const clang::Decl* d) {
                       return llvm::isa<clang::VarDecl>(d);
                   });
        }

        // The returned expression when function is a small factory: not std/sol2, body of type declarations and
        // static_asserts followed by one `return expr;` (`self_return<&Vec2::rotate>()`).
        const clang::Expr* factoryResult(const clang::FunctionDecl& function) {
            const std::string name = function.getQualifiedNameAsString();
            if (llvm::StringRef(name).starts_with("std::") || llvm::StringRef(name).starts_with("sol::")) {
                return nullptr;
            }
            const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(function.getBody());
            if (body == nullptr || body->size() == 0 ||
                !std::all_of(body->body_begin(), body->body_end() - 1, declaresNoVariable)) {
                return nullptr;
            }
            const auto* returned = llvm::dyn_cast<clang::ReturnStmt>(body->body_back());
            return returned == nullptr ? nullptr : returned->getRetValue();
        }

        // A field or method passed as a template argument (`<&Particle::x>`, `<&Vec2::rotate>`), which then documents
        // the factory's result.
        const clang::ValueDecl* templateMember(const clang::FunctionDecl& function) {
            const clang::TemplateArgumentList* arguments = function.getTemplateSpecializationArgs();
            if (arguments == nullptr) {
                return nullptr;
            }
            for (const clang::TemplateArgument& argument : arguments->asArray()) {
                if (argument.getKind() == clang::TemplateArgument::Declaration &&
                    llvm::isa<clang::FieldDecl, clang::IndirectFieldDecl, clang::CXXMethodDecl>(argument.getAsDecl())) {
                    return argument.getAsDecl();
                }
            }
            return nullptr;
        }

        // One pass-through step of definitionOf, or nullptr when e defines the value itself.
        const clang::Expr* passedThrough(const clang::Expr* e, Definition& definition) {
            if (const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(e)) {
                const auto* variable = llvm::dyn_cast<clang::VarDecl>(reference->getDecl());
                return variable != nullptr && variable->isLocalVarDecl() ? variable->getInit() : nullptr;
            }
            const auto* call = llvm::dyn_cast<clang::CallExpr>(e);
            const clang::FunctionDecl* callee = call == nullptr ? nullptr : call->getDirectCallee();
            if (callee == nullptr) {
                return nullptr;
            }
            if (callee->getQualifiedNameAsString() == "std::move" && call->getNumArgs() == 1) {
                return call->getArg(0);
            }
            const clang::Expr* result = factoryResult(*callee);
            if (result != nullptr && definition.documentedMember == nullptr) {
                definition.documentedMember = templateMember(*callee);
            }
            return result;
        }

        void addBound(const clang::Expr* e, std::vector<BoundFunction>& out, const clang::ValueDecl* documentedBy);

        // sol::overload(a, b, ...) binds each argument; sol::resolve<Sig>(f) binds the overload Clang picked.
        void addBoundCall(const clang::CallExpr& call, std::vector<BoundFunction>& out) {
            const std::string name = calleeName(call);
            if (name == "sol::overload") {
                for (const clang::Expr* argument : call.arguments()) {
                    addBound(argument, out, nullptr);
                }
            }
            else if (name == "sol::resolve" && call.getNumArgs() == 1) {
                addBound(call.getArg(0), out, nullptr);
            }
        }

        // Appends the functions e binds to out; documentedBy comes from a factory around e.
        void addBound(const clang::Expr* e, std::vector<BoundFunction>& out, const clang::ValueDecl* documentedBy) {
            const Definition definition = definitionOf(e);
            const clang::Expr* value = definition.expr;
            const clang::ValueDecl* documented =
                definition.documentedMember != nullptr ? definition.documentedMember : documentedBy;
            if (const auto* lambda = llvm::dyn_cast<clang::LambdaExpr>(value)) {
                out.push_back({lambda->getCallOperator(), documented});
            }
            else if (const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(value)) {
                if (const auto* function = llvm::dyn_cast<clang::FunctionDecl>(reference->getDecl())) {
                    out.push_back({function, documented});
                }
            }
            else if (const auto* address = llvm::dyn_cast<clang::UnaryOperator>(value)) {
                if (address->getOpcode() == clang::UO_AddrOf) {
                    addBound(address->getSubExpr(), out, documented);
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

    } // namespace

    KeyPath keyPathOf(const clang::Expr* target, const clang::ASTContext& context) {
        KeyPath path;
        const clang::Expr* object = target;
        while (const clang::CXXOperatorCallExpr* subscript = asSubscript(object)) {
            path.keys.push_back(keyText(subscript->getArg(1), context));
            if (path.lastKey == nullptr) {
                path.lastKey = subscript->getArg(1);
            }
            object = subscript->getArg(0)->IgnoreImplicit();
        }
        std::ranges::reverse(path.keys);
        path.root = object;
        return path;
    }

    std::string sourceText(const clang::Expr* e, const clang::ASTContext& context) {
        const clang::CharSourceRange range = clang::CharSourceRange::getTokenRange(e->getSourceRange());
        return clang::Lexer::getSourceText(range, context.getSourceManager(), context.getLangOpts()).str();
    }

    std::string recordName(clang::QualType type) {
        const clang::CXXRecordDecl* record = type.getNonReferenceType()->getAsCXXRecordDecl();
        return record == nullptr ? std::string() : record->getQualifiedNameAsString();
    }

    std::string templateName(clang::QualType type) {
        const auto* specialization = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
            type.getNonReferenceType()->getAsCXXRecordDecl());
        return specialization == nullptr ? std::string()
                                         : specialization->getSpecializedTemplate()->getQualifiedNameAsString();
    }

    std::vector<clang::QualType> templateTypeArguments(clang::QualType type) {
        std::vector<clang::QualType> types;
        const auto* specialization = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
            type.getNonReferenceType()->getAsCXXRecordDecl());
        if (specialization == nullptr) {
            return types;
        }
        for (const clang::TemplateArgument& argument : specialization->getTemplateArgs().asArray()) {
            if (argument.getKind() == clang::TemplateArgument::Type) {
                types.push_back(argument.getAsType());
            }
            else if (argument.getKind() == clang::TemplateArgument::Pack) {
                for (const clang::TemplateArgument& element : argument.pack_elements()) {
                    types.push_back(element.getAsType());
                }
            }
        }
        return types;
    }

    std::string functionKey(const clang::FunctionDecl& function) {
        const clang::PrintingPolicy policy(function.getASTContext().getLangOpts());
        std::string key;
        llvm::raw_string_ostream out(key);
        function.getNameForDiagnostic(out, policy, true);
        out << '(';
        for (const clang::ParmVarDecl* param : function.parameters()) {
            out << param->getType().getCanonicalType().getAsString(policy) << ',';
        }
        out << ')';
        // Lambdas print without their location; tell them apart by it (absolute, so it is only a join key).
        const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
        if (method != nullptr && method->getParent()->isLambda()) {
            out << '@' << method->getParent()->getLocation().printToString(function.getASTContext().getSourceManager());
        }
        return key;
    }

    std::string parameterKey(const clang::FunctionDecl& function, unsigned index) {
        return functionKey(function) + '#' + std::to_string(index);
    }

    bool isSolObject(clang::QualType type) { return llvm::StringRef(recordName(type)).starts_with("sol::"); }

    bool isLuaState(clang::QualType type) {
        const std::string name = recordName(type);
        return name == "sol::state" || name == "sol::state_view" || name == "sol::basic_environment";
    }

    bool isScriptResult(const clang::Expr* e) {
        const auto* call = llvm::dyn_cast<clang::CXXMemberCallExpr>(e->IgnoreImplicit());
        const clang::CXXMethodDecl* method = call == nullptr ? nullptr : call->getMethodDecl();
        return method != nullptr && (method->getName() == "safe_script" || method->getName() == "script");
    }

    std::string calleeName(const clang::CallExpr& call) {
        const clang::FunctionDecl* callee = call.getDirectCallee();
        return callee == nullptr ? std::string() : callee->getQualifiedNameAsString();
    }

    std::optional<std::string> stringValue(const clang::Expr* e) {
        // withoutConversions: a `string_view` parameter (new_enum's name) constructs from the literal.
        const auto* literal = llvm::dyn_cast<clang::StringLiteral>(stripped(withoutConversions(e)));
        return literal == nullptr ? std::nullopt : std::optional<std::string>(literal->getString().str());
    }

    std::optional<std::string> enumeratorName(const clang::Expr* e) {
        const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(stripped(e));
        const auto* enumerator =
            reference == nullptr ? nullptr : llvm::dyn_cast<clang::EnumConstantDecl>(reference->getDecl());
        return enumerator == nullptr ? std::nullopt : std::optional<std::string>(enumerator->getNameAsString());
    }

    const clang::Expr* withoutConversions(const clang::Expr* e) {
        for (int step = 0; step < MaxDefinitionSteps && e != nullptr; ++step) {
            e = e->IgnoreImplicit();
            if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(e);
                construct != nullptr && construct->getNumArgs() == 1) {
                e = construct->getArg(0);
            }
            else if (const auto* call = llvm::dyn_cast<clang::CXXMemberCallExpr>(e);
                     call != nullptr && llvm::isa_and_present<clang::CXXConversionDecl>(call->getMethodDecl())) {
                e = call->getImplicitObjectArgument();
            }
            else {
                return e;
            }
        }
        return e;
    }

    Definition definitionOf(const clang::Expr* e) {
        Definition definition{stripped(e), nullptr};
        for (int step = 0; step < MaxDefinitionSteps; ++step) {
            const clang::Expr* next = passedThrough(definition.expr, definition);
            if (next == nullptr) {
                break;
            }
            definition.expr = stripped(next);
        }
        return definition;
    }

    std::vector<BoundFunction> boundFunctions(const clang::Expr* e) {
        std::vector<BoundFunction> functions;
        addBound(e, functions, nullptr);
        return functions;
    }

    FunctionSig signatureOf(const BoundFunction& bound, const SourceRoot& root, const clang::ASTContext& context) {
        FunctionSig sig = signatureOf(*bound.function, root, context);
        if (sig.doc.lines.empty() && bound.documentedBy != nullptr) {
            sig.doc = docOfDecl(*bound.documentedBy, context);
        }
        return sig;
    }

    FunctionSig signatureOf(const clang::FunctionDecl& function, const SourceRoot& root,
                            const clang::ASTContext& context) {
        FunctionSig sig;
        for (const clang::ParmVarDecl* param : function.parameters()) {
            Param& added = sig.params.emplace_back();
            added.name = param->getNameAsString();
            added.type = classifyType(param->getType(), context);
            added.key = parameterKey(function, param->getFunctionScopeIndex());
        }
        markOmittable(sig.params);
        sig.returns = classifyReturns(function.getReturnType(), context);
        const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
        if (method != nullptr && method->getParent()->isLambda()) {
            const std::optional<SourceLoc> loc = root.locate(context.getSourceManager(), function.getLocation());
            sig.cppTarget = loc ? "lambda at " + loc->file + ":" + std::to_string(loc->line) : "lambda";
            return sig;
        }
        sig.cppTarget = function.getQualifiedNameAsString();
        sig.doc = docOfDecl(function, context);
        return sig;
    }

} // namespace dd
