#pragma once

#include "model.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/Basic/SourceLocation.h>
#include <llvm/ADT/StringRef.h>

#include <optional>
#include <string>
#include <vector>

namespace dd {

    // The `///` block whose last line is directly above loc's line (a blank line or any other comment in between
    // detaches it, as in the old parser); empty when there is none.
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

    // A key and value pair commented out inside an enum call: `//, "FLOOR_BORDERTILE", 1`.
    struct CommentedPair {
        // The key without quotes; empty for hints such as `"", ...check [entities.txt]...`.
        std::string luaName;
        // The value text.
        std::string text;
        // Start of the pair's comment.
        clang::SourceLocation loc;
    };

    // The commented-out pairs on the lines of range (the call that creates an enum).
    std::vector<CommentedPair> commentedPairs(clang::SourceRange range, const clang::ASTContext& context);

    // The `/* NAME // KEY // doc */` blocks in file: a block comment starting a line whose first line is one name.
    std::vector<BlockDoc> blockDocs(clang::FileID file, const clang::SourceManager& sm);

    // The member named by a commented-out member reference just above loc, written for the old regex parser:
    // `// &Vec2::split); // for the autodoc` above a value, or `// ut["pause"] = &PauseAPI::pause;` above a
    // `/// NoDoc` binding. Only the member name is returned: some hints name the wrong class after copy-paste.
    std::optional<std::string> autodocHint(clang::SourceLocation loc, const clang::ASTContext& context);

    // A callback signature written in a doc comment: `The callback signature is bool on_x(Entity self)` or
    // ``the function `nil do_spawn(float x, float y)` ``.
    struct ProseCallback {
        // The callback's name in the text, which may name the parameter that receives it (`do_spawn`).
        std::string name;
        // Its parameters and return as text only (origin DocComment), and the matched text as prose.
        CallbackSig callback;
    };

    // Every callback signature written in lines (the old reSignature regex, ported).
    std::vector<ProseCallback> proseCallbacks(const std::vector<std::string>& lines);

    // The callback an event's docs describe: params from a `Params: T a, U b` line, a return when a `Return: ...`
    // line exists (text only); prose is the Params line.
    CallbackSig eventProse(const std::vector<std::string>& lines);

    // Parameters and return of lambda text such as `[](uint32_t uid) -> Entity*{}`; types keep only their
    // spelling (kind Unknown), since the text is not compiled.
    FunctionSig signatureFromText(llvm::StringRef text);

} // namespace dd
