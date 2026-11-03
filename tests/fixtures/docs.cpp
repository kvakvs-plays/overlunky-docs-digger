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

// An autodoc hint: the commented-out binding documents the function for the old parser, and the `/// NoDoc` only hides
// the real binding (an overload set of wrappers) from it. Lua sees a documented function.
void register_hinted(sol::state& lua)
{
    // lua["convert_id"] = convert_id;
    /// NoDoc
    lua["convert_id"] = sol::overload([](int id)
                                      { return id + 1; },
                                      [](float id)
                                      { return id + 1.0f; });
}
