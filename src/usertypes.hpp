#pragma once

#include "model.hpp"
#include "source_root.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/ExprCXX.h>

#include <optional>
#include <string>

// Pass B2: usertypes from `new_usertype<T>("Name", ...)`, their members from its arguments and from later
// `ut["k"] = v` / `lua["Name"]["k"] = v` bindings.
namespace dd {

    // T of a `sol::usertype<T>` object (the result of new_usertype), or nothing for other types.
    std::optional<clang::QualType> usertypeOf(clang::QualType type);

    // The key Extraction::usertypes uses for the C++ type T.
    std::string usertypeKey(clang::QualType type);

    // Describes the member value binds; record (T, may be null) tells which first parameters are the object.
    Member describeMember(const clang::Expr* value, const clang::CXXRecordDecl* record, const SourceRoot& root,
                          const clang::ASTContext& context);

    // Documents member like the old parser: by the C++ member an autodoc hint above loc names (see autodocHint),
    // always; otherwise, when no comment of the bound code documents it, by the C++ member of the same name (a
    // lambda `"topmost"` that calls `entity.topmost()`). Names are looked up in record and its bases.
    void applyDocFallbacks(Member& member, clang::SourceLocation loc, const clang::CXXRecordDecl* record,
                           const clang::ASTContext& context);

    // Records the usertype created by a `new_usertype<T>(...)` call and the members in its arguments.
    void addUsertype(const clang::CXXMemberCallExpr& call, const SourceRoot& root, const clang::ASTContext& context,
                     Extraction& out);

    // Moves `lua["Name"]["k"] = v` bindings into the members of the usertype named Name (sol2 stores them in the
    // usertype's table, so Lua sees them as members). Run once every translation unit is parsed.
    void attachOwnedBindings(Extraction& extraction);

} // namespace dd
