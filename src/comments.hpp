#pragma once

#include "model.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/Basic/SourceLocation.h>
#include <llvm/ADT/StringRef.h>

#include <string>
#include <vector>

namespace dd {

    // The `///` block whose last line is directly above loc's line (a blank line in between detaches it, as in
    // the old parser); empty when there is none.
    Doc docBefore(clang::SourceLocation loc, const clang::ASTContext& context);

    // The `///` comment of decl or of any of its redeclarations (headers document functions, .cpp files define
    // them).
    Doc docOfDecl(const clang::Decl& decl, const clang::ASTContext& context);

    // Comment lines with the leading `NoDoc` / `Deprecated` directive lines turned into flags.
    Doc parseDoc(std::vector<std::string> lines);

    // A function written in Lua and documented in C++ by a `///` block followed by a commented-out binding,
    // `// lua["get_entity"] = [](uint32_t uid) -> Entity*{};`.
    struct CommentedBinding {
        // Lua name from the commented-out key.
        std::string luaName;
        // Everything after `=`: the lambda text that stands for the signature.
        std::string signature;
        // Start of the commented-out line.
        clang::SourceLocation loc;
        // The `///` block above it.
        Doc doc;
    };

    // All commented-out bindings directly below a `///` block in file.
    std::vector<CommentedBinding> commentedBindings(clang::FileID file, const clang::ASTContext& context);

    // Parameters and return of lambda text such as `[](uint32_t uid) -> Entity*{}`; types keep only their
    // spelling (kind Unknown), since the text is not compiled.
    FunctionSig signatureFromText(llvm::StringRef text);

} // namespace dd
