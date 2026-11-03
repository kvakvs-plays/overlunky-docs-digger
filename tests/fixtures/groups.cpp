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

// DIG-7 type groups: rules on C++ bases and Lua names, a rule that needs a base, the directive and the fallback.
struct Entity
{
    int uid;
};
struct Movable : Entity
{
    float x;
};
struct Floor : Entity
{
    int deco;
};
struct LogicalDoor : Entity
{
    int target;
};
struct LogicalSettings
{
    int flags;
};
struct Odd
{
    int value;
};
struct Plain
{
    int value;
};

void register_group_types(sol::state& lua)
{
    /// The root of the hierarchy: matched by its whole name.
    lua.new_usertype<Entity>("Entity", "uid", &Entity::uid);
    /// Matched by a base.
    lua.new_usertype<Movable>("Movable", "x", &Movable::x, sol::base_classes, sol::bases<Entity>());
    /// Floor comes first in the rules, so it wins over the Entity base.
    lua.new_usertype<Floor>("Floor", "deco", &Floor::deco, sol::base_classes, sol::bases<Entity>());
    /// Has the base the Logical rule requires.
    lua.new_usertype<LogicalDoor>("LogicalDoor", "target", &LogicalDoor::target, sol::base_classes, sol::bases<Entity>());
    /// Logical in the name but no Entity base: the rule does not apply.
    lua.new_usertype<LogicalSettings>("LogicalSettings", "flags", &LogicalSettings::flags);
    /// Group: Odd types
    lua.new_usertype<Odd>("Odd", "value", &Odd::value);
    lua.new_usertype<Plain>("Plain", "value", &Plain::value);
}
