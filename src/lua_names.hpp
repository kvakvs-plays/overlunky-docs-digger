#pragma once

#include "model.hpp"

#include <llvm/Support/Error.h>

#include <map>
#include <set>
#include <string>

// Lua type names (plan P16): what the LuaLS files and the HTML reference write for each C++ type, built from the type
// facts. No Clang AST here.
namespace dd {

    // What luaType() looks names up in.
    struct LuaNaming {
        // Usertype Lua names by canonical C++ type (TypeInfo::canonical of a record).
        std::map<std::string, std::string> usertypes;
        // Lua enum table names (`ON`, `ENT_TYPE`): C++ enums and typedefs of that name are written as the table.
        std::set<std::string> enums;
        // Kept alias names (`CallbackId`): written as themselves, the LuaLS files declare them.
        std::set<std::string> aliases;
        // config/lua_types.json: Lua types for C++ names the facts cannot name (`ImVec2` → `Vec2`), by canonical
        // type, spelling or unqualified name; also the words of text-only types (`uint32_t` → `integer`).
        std::map<std::string, std::string> names;
    };

    // Reads a Lua types file (`{"names": {"<C++ name>": "<Lua type>"}}`) into names.
    llvm::Error loadLuaTypes(const std::string& path, std::map<std::string, std::string>& names);

    // The lookup tables of extraction (usertypes, enums, kept aliases), with the configured names.
    LuaNaming luaNaming(const Extraction& extraction, std::map<std::string, std::string> names);

    // The LuaLS spelling of type: `integer`, `Entity?`, `ENT_TYPE[]`, `table<string, integer>`, `[integer, number]`,
    // `fun(e: Entity): boolean`, `any`. Inner types are named the same way, whether their `lua` is set or not.
    std::string luaType(const TypeInfo& type, const LuaNaming& naming);

    // Sets TypeInfo::lua on every type in extraction, inner and returned types included.
    void assignLuaNames(Extraction& extraction, const LuaNaming& naming);

} // namespace dd
