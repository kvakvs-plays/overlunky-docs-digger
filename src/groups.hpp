#pragma once

#include "model.hpp"

#include <llvm/Support/Error.h>

#include <string>
#include <vector>

// Function and type groups (plan P14, DIG-7): the documentation sections functions and usertypes are listed in. No
// Clang AST here.
namespace dd {

    // One rule of config/groups.json: a function belongs to the group when any of its tests matches.
    struct GroupRule {
        // Section name (`Spawn functions`).
        std::string group;
        // Substrings of the Lua name, as generate.py's func_cats tests them.
        std::vector<std::string> nameContains;
        // Whole Lua names, for names a substring would over-match (`door`).
        std::vector<std::string> names;
        // Substrings of the root-relative source file (`game_patches_lua.cpp`).
        std::vector<std::string> fileContains;
    };

    // Where a usertype is listed: a group inside a section, as generate.py's type_cats nest them.
    struct TypePlace {
        // Group name (`Floor entities`).
        std::string group;
        // Section that holds the group (`Entity types`).
        std::string section;
    };

    // One type rule of config/groups.json: a usertype belongs to the place when it has one of the required bases (if
    // any are given) and any of the other tests matches.
    struct TypeRule {
        // Group and section the rule assigns.
        TypePlace place;
        // Substrings one of the C++ bases must contain for the rule to apply at all (`Entity` for `Logical`).
        std::vector<std::string> requiresBase;
        // Substrings of a C++ base, as generate.py tests the joined base list (`Floor`).
        std::vector<std::string> basesContain;
        // Substrings of the Lua name.
        std::vector<std::string> nameContains;
        // Whole Lua names, for the root of a hierarchy (`Floor` itself).
        std::vector<std::string> names;
    };

    // The contents of a groups file (config/groups.json): rules tried in order, then a catch-all group.
    struct GroupConfig {
        // The first matching rule decides.
        std::vector<GroupRule> rules;
        // Group of the functions no rule matches; empty to derive it from the source file.
        std::string fallback;
        // Usertype rules, tried in order like the function rules.
        std::vector<TypeRule> typeRules;
        // Place of the usertypes no type rule matches; an empty group derives it from the source file.
        TypePlace typeFallback;
    };

    // Reads a groups file (format in README.md); the error names the file or the bad JSON path.
    llvm::Expected<GroupConfig> loadGroups(const std::string& path);

    // Gives every function its group: the `/// Group:` directive, else the table it is bound in (namespace tables),
    // else the first matching rule, else the config's fallback, else its source file (`spawn_lua.cpp` → `spawn`).
    // Gives every usertype its group and section the same way, from the type rules (there is no owner table).
    void assignGroups(Extraction& extraction, const GroupConfig& config);

} // namespace dd
