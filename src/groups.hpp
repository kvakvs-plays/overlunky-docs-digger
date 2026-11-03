#pragma once

#include "model.hpp"

#include <llvm/Support/Error.h>

#include <string>
#include <vector>

// Function groups (plan P14): the documentation sections functions are listed in. No Clang AST here.
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

    // The contents of a groups file (config/groups.json): rules tried in order, then a catch-all group.
    struct GroupConfig {
        // The first matching rule decides.
        std::vector<GroupRule> rules;
        // Group of the functions no rule matches; empty to derive it from the source file.
        std::string fallback;
    };

    // Reads a groups file (format in README.md); the error names the file or the bad JSON path.
    llvm::Expected<GroupConfig> loadGroups(const std::string& path);

    // Gives every function its group: the `/// Group:` directive, else the table it is bound in (namespace tables),
    // else the first matching rule, else the config's fallback, else its source file (`spawn_lua.cpp` → `spawn`).
    void assignGroups(Extraction& extraction, const GroupConfig& config);

} // namespace dd
