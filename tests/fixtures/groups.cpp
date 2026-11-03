// P14 function groups: the `/// Group:` directive, namespace tables, the rules of groups.groups.json (name
// substrings, whole names, file substrings, first match wins) and the config's fallback.
#include <sol/sol.hpp>

#include "groups_patches.hpp"

void register_groups(sol::state& lua)
{
    /// Matches the first rule by a substring of its name.
    lua["spawn_thing"] = [](int count)
    { return count; };

    /// Both rules match; the first one listed wins.
    lua["spawn_entity"] = []()
    { return 0; };

    /// Matches the second rule by its whole name.
    lua["door"] = []()
    { return 0; };

    /// No rule matches, so the fallback of the config applies.
    lua["frobnicate"] = []()
    { return 0; };

    /// Spawns, but the directive overrides the rules.
    /// Group: Special functions
    lua["spawn_special"] = []()
    { return 0; };

    /// Group: Special functions
    /// NoDoc
    /// A Group line first still lets the NoDoc after it be read.
    lua["hidden_special"] = []()
    { return 0; };

    lua.create_named_table("Util");
    /// Bound in a namespace table: grouped by the table.
    lua["Util"]["spawn_helper"] = []()
    { return 0; };

    register_patches(lua);
}
