// B1 functions: every form resolveBound() accepts, and the type families TypeInfo classifies.
#include <sol/sol.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

enum class LAYER : int32_t
{
    FRONT = 0,
    BACK = 1,
};
using ENT_TYPE = uint32_t;
struct Entity
{
    int uid;
};

int spawn(ENT_TYPE type, float x, float y, LAYER layer);
int spawn(ENT_TYPE type, float x, float y, LAYER layer)
{
    return static_cast<int>(type) + static_cast<int>(x + y) + static_cast<int>(layer);
}
std::vector<uint32_t> get_entities_by(ENT_TYPE type, LAYER layer);
std::vector<uint32_t> get_entities_by(std::vector<ENT_TYPE> types, LAYER layer);
int count(int a);
int count(int a, int b);

void register_functions(sol::state& lua)
{
    lua["spawn"] = spawn;
    lua["spawn_address"] = &spawn;
    lua["count_one"] = sol::resolve<int(int)>(count);
    lua["get_entities_by"] = sol::overload(
        static_cast<std::vector<uint32_t> (*)(ENT_TYPE, LAYER)>(::get_entities_by),
        static_cast<std::vector<uint32_t> (*)(std::vector<ENT_TYPE>, LAYER)>(::get_entities_by));
    lua["set_flag"] = sol::overload(
        [](int uid, bool value)
        { return uid != 0 && value; },
        [](std::string name)
        { return !name.empty(); });

    lua["optional_args"] = [](int uid, std::optional<int> depth, sol::optional<std::string> label) -> void
    { (void)uid; (void)depth; (void)label; };
    lua["pointer_arg"] = [](Entity* entity, uint32_t mask)
    { return entity != nullptr && mask != 0; };
    lua["hidden_state"] = [](sol::this_state state, int uid, Entity* target) -> bool
    { (void)state; return uid != 0 && target != nullptr; };
    lua["variadic"] = [](sol::variadic_args args) -> int
    { return static_cast<int>(args.size()); };
    lua["tuple_return"] = []() -> std::tuple<int, float, std::string>
    { return {1, 2.0f, "three"}; };
    lua["pair_return"] = []() -> std::pair<bool, LAYER>
    { return {true, LAYER::BACK}; };
    lua["optional_return"] = [](int uid) -> std::optional<Entity*>
    { (void)uid; return std::nullopt; };
    lua["containers"] = [](std::map<std::string, std::vector<ENT_TYPE>> table) -> std::vector<std::string>
    { return {table.empty() ? "" : table.begin()->first}; };
    lua["callbacks"] = [](sol::function cb, std::function<bool(int, std::string)> filter) -> sol::object
    { (void)cb; (void)filter; return sol::lua_nil; };

    lua["Ns"] = lua.create_named_table("Ns");
    lua["Ns"]["nested"] = [](float value) -> float
    { return value * 2; };

    auto spawn_tree = sol::overload(
        static_cast<int (*)(int)>(::count),
        static_cast<int (*)(int, int)>(::count));
    lua["spawn_tree"] = spawn_tree;
    lua["spawn_alias"] = lua["spawn"];
    lua["printf"] = lua.safe_script(R"(
        return function(...)
            print(string.format(...))
        end
    )");

    lua["on_frame"] = sol::lua_nil;
    lua["answer"] = 42;
    lua["__script_id"] = "fixture/functions";
    sol::table entity_type = lua.create_table();
    entity_type["uid"] = &Entity::uid;

    // A script's environment holds that script's globals.
    sol::environment env(lua, sol::create, lua.globals());
    env["on_level"] = sol::lua_nil;
}

// A value made by a factory that casts an offset to a pointer: its type is the pointer, not the offset.
struct SaveData
{
    int deaths;
};
template <typename T>
struct OnHeap
{
    std::size_t offset;
    T* decode() const
    {
        return reinterpret_cast<T*>(offset + 16);
    }
};

void register_save(sol::state& lua, const OnHeap<SaveData>& save)
{
    /// The save data.
    lua["savegame"] = save.decode();
}
