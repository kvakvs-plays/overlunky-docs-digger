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

    // C5: adds the vtable hooks to their usertypes (by Lua name) and drops the generic records B1 made of the
    // registration lambdas.
    void attachVtableHooks(Extraction& extraction);

    // C1 to C3: gives every Lua function parameter its callback: from a std::function type, or from the flow found
    // where its function is defined, with stored callbacks resolved through their dispatch sites and callbacks
    // forwarded to another Lua function copied from it.
    void attachCallbacks(Extraction& extraction);

    // C4: one Event per key of every enum-keyed callback storage; for storages whose binding takes that enum, every
    // value of the Lua table gets one (signature of its sites, of the generic dispatch, or unresolved).
    void buildEvents(Extraction& extraction);

    // C7: matches the `The callback signature is ...` prose of functions and members, and the `Params:` /
    // `Return:` lines of events, with the extracted callbacks: prose fills unresolved ones, disagreements become
    // conflicts.
    void checkProse(Extraction& extraction);

} // namespace dd
