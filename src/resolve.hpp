#pragma once

#include "model.hpp"
#include "source_root.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>

#include <optional>
#include <string>
#include <vector>

// Reading sol2 binding expressions: key paths, the expression that defines a bound value, and the C++ functions
// it binds (plan B1 `resolveBound`). Shared by the table assignment and usertype passes.
namespace dd {

    // The keys of `root["a"]["b"]` and the root expression.
    struct KeyPath {
        // Keys from the root outwards; a key that is not a string literal is kept as `[source text]`.
        std::vector<std::string> keys;
        // The object the first operator[] is applied to.
        const clang::Expr* root = nullptr;
    };

    // Splits the left side of an assignment into its keys and root.
    KeyPath keyPathOf(const clang::Expr* target, const clang::ASTContext& context);

    // The code as written, for keys and roots that are not string literals.
    std::string sourceText(const clang::Expr* e, const clang::ASTContext& context);

    // Qualified name of the class of type, or empty for non-class types.
    std::string recordName(clang::QualType type);

    // Qualified name of the class template type is a specialization of, or empty.
    std::string templateName(clang::QualType type);

    // True for sol2 objects (state, state_view, table, proxy, usertype): plain maps are not Lua tables.
    bool isSolObject(clang::QualType type);

    // True when the root is the Lua state or a script's environment (whose keys are that script's globals).
    bool isLuaState(clang::QualType type);

    // `lua.safe_script(...)` / `lua.script(...)`: a value computed by Lua code, here a function written in Lua.
    bool isScriptResult(const clang::Expr* e);

    // Qualified name of the function a call goes to, or empty.
    std::string calleeName(const clang::CallExpr& call);

    // The text of a string literal argument, or nothing for other expressions.
    std::optional<std::string> stringValue(const clang::Expr* e);

    // The name of the enumerator e refers to (`sol::lib::math` → `math`), or nothing.
    std::optional<std::string> enumeratorName(const clang::Expr* e);

    // e without the implicit conversions sol2 proxies go through when a declaration reads them
    // (`sol::optional<sol::function> f = lua["f"];` constructs from a conversion operator call).
    const clang::Expr* withoutConversions(const clang::Expr* e);

    // The expression that defines a bound value, after following what only passes it on.
    struct Definition {
        // The defining expression (implicit nodes, parentheses and casts stripped).
        const clang::Expr* expr = nullptr;
        // A field named by a factory's template argument (`MakeParticleMemberAccess<&Particle::x>()`), whose
        // comment documents the value.
        const clang::ValueDecl* documentedMember = nullptr;
    };

    // Follows `std::move(x)`, local variables (`auto overlay = sol::property(...)`) and small factory functions
    // whose body is one `return` to the expression that defines e.
    Definition definitionOf(const clang::Expr* e);

    // One C++ function a value binds.
    struct BoundFunction {
        // The function or lambda call operator Lua calls.
        const clang::FunctionDecl* function = nullptr;
        // The member a wrapping factory names (`self_return<&AABB::extrude>()`), documenting the function when it
        // has no comment of its own.
        const clang::ValueDecl* documentedBy = nullptr;
    };

    // resolveBound: the C++ functions e binds (lambdas, function names, `&f`, casts, sol::overload, sol::resolve);
    // empty when e is not callable.
    std::vector<BoundFunction> boundFunctions(const clang::Expr* e);

    // The signature of one bound C++ function or lambda call operator; lambdas are named by their location.
    FunctionSig signatureOf(const clang::FunctionDecl& function, const SourceRoot& root,
                            const clang::ASTContext& context);

    // The signature of a bound function, documented by the wrapped member when it has no comment of its own.
    FunctionSig signatureOf(const BoundFunction& bound, const SourceRoot& root, const clang::ASTContext& context);

} // namespace dd
