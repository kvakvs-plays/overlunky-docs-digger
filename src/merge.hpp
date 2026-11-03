#pragma once

#include "model.hpp"

#include <string>

// Steps run once every translation unit is parsed, on the merged extraction (no Clang AST here).
namespace dd {

    // Adds the tables and values of a `lua_enums.txt` dump of a running game (`NAME = {`, `  KEY = value,`, `}`)
    // that the sources do not have; false when the file cannot be read.
    bool addRuntimeEnums(Extraction& extraction, const std::string& path);

    // Matches the block docs with enum values and usertype members by key, filling docs that are still empty.
    void attachBlockDocs(Extraction& extraction);

    // Drops named tables without values (namespaces such as `lua.create_named_table("TYPE_MAP")`).
    void dropEmptyEnums(Extraction& extraction);

    // Keeps the aliases that some Lua-visible type is written with, directly or through another kept alias.
    void keepUsedAliases(Extraction& extraction);

    // Lists the documented values bound on the state as the API's global variables.
    void collectGlobalVariables(Extraction& extraction);

} // namespace dd
