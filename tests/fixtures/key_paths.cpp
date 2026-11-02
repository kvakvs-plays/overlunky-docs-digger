// B1 key paths: assignments on sol2 objects through operator[] chains, as overlunky writes them.
#include <sol/sol.hpp>

#include <map>
#include <string>

namespace NS
{
int add(int a, int b)
{
    return a + b;
}
} // namespace NS

void register_things(sol::state& lua)
{
    lua["add"] = NS::add;
    lua["twice"] = [](int x)
    { return 2 * x; };

    lua.create_named_table("Ns");
    lua["Ns"]["nested"] = [](int x)
    { return x; };

    sol::table table = lua.create_table();
    table["inner"] = 1;

    const std::string key = "computed";
    lua[key] = 2;

    // Not a Lua table: must not be reported.
    std::map<std::string, int> plain;
    plain["ignored"] = 3;
}

void register_view(sol::state_view view)
{
    view["from_view"] = 4;
}
