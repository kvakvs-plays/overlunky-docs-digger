// B1 documentation: statement comments, directives, declaration comments, commented-out bindings.
#include <sol/sol.hpp>

#include <cstdint>

/// Get the player count, from the declaration.
/// Returns: 0 outside of a level
int get_player_count();
int get_player_count()
{
    return 4;
}

void register_docs(sol::state& lua)
{
    /// Documented on the binding.
    /// Default: `depth` is 1
    lua["documented"] = [](int depth)
    { return depth; };

    /// Detached by the blank line below, so not attached.

    lua["undocumented"] = []()
    { return 0; };

    /// NoDoc
    /// Internal helper.
    lua["internal"] = []()
    { return 1; };

    /// Deprecated
    /// Use `documented` instead.
    lua["old_name"] = [](int depth)
    { return depth; };

    lua["get_player_count"] = get_player_count;

    /// Get the Entity behind an uid, converted to the correct type.
    // lua["get_entity"] = [](uint32_t uid) -> Entity*{};

    /// Dump the object as a recursive table.
    // lua["dump"] = [](sol::object object, optional<int> depth) -> table

    /// Table of options set in the UI, added with the `register_option_...` functions.
    // lua["options"] = lua.create_named_table("options");
}
