// P14 function groups: a binding in this file is grouped by the file name rule of groups.groups.json.
#pragma once

#include <sol/sol.hpp>

inline void register_patches(sol::state& lua)
{
    /// Grouped by the file it is bound in.
    lua["modify_ankh_health"] = [](int max)
    { return max; };
}
