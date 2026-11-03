#pragma once

#include "model.hpp"

#include <llvm/Support/Error.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// LuaLS definition files (plan DIG-6): `---@meta` files that declare the extracted API to the Lua language server.
// Written from the merged extraction and the Lua type names (lua_names.hpp); no Clang AST here.
namespace dd {

    // Which annotation dialect the Lua files use.
    enum class LuaStyle : std::uint8_t {
        // lua-language-server (the VS Code Lua extension): `T?`, `name?`, tuples, `---@operator`, literal types.
        LuaLS,
        // Classic EmmyLua (older IDE plugins): `T|nil` instead of `?`, `table` for tuples, no `---@operator`,
        // descriptions after `@`.
        EmmyLua,
    };

    // How the Lua files are written.
    struct LuaFileSettings {
        // Annotation dialect.
        LuaStyle style = LuaStyle::LuaLS;
        // Hand-written declarations put after the header of the globals file (config/lua_prelude.lua): what the C++
        // side does not declare (`json`, `inspect`, the `Meta` class).
        std::string prelude;
        // Base URL that makes the `(#anchor)` links of the docs absolute; empty keeps them.
        std::string docUrl;
        // A file that would grow past this many bytes continues in the next one (`spel2_types_2.lua`).
        std::size_t maxFileSize = 480000;
    };

    // One definition file.
    struct LuaFile {
        // File name (`spel2_types.lua`).
        std::string name;
        // Its contents, `\n` line ends.
        std::string text;
    };

    // The definition files of extraction: `spel2.lua` (global variables, script callbacks, functions by group,
    // aliases), `spel2_types.lua` (usertypes) and `spel2_enums.lua` (enums), each continued in `_2`, `_3` when over
    // the size limit. `/// NoDoc` items are left out.
    std::vector<LuaFile> luaFiles(const Extraction& extraction, const LuaFileSettings& settings);

    // Writes files into directory (created when missing), after removing the `spel2*.lua` files of a previous run.
    llvm::Error writeLuaFiles(const std::vector<LuaFile>& files, const std::string& directory);

} // namespace dd
