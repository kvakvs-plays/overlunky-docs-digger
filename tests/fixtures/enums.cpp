// B3, B4 and globals: enum tables, aliases, libraries, global variables and script callbacks, as overlunky writes them.
#include <sol/sol.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using CallbackId = int32_t;
using ENT_TYPE = uint32_t;
/// Not used by any binding.
using Unused = int64_t;
using Ids = std::vector<ENT_TYPE>;
typedef uint8_t SHORT_TILE_CODE;
template <class T>
using Holder = std::vector<T>;

enum class LAYER : int32_t
{
    FRONT = 0,
    BACK = 1,
    PLAYER = -1,
};

namespace FMODStudio
{
enum class StopMode
{
    AllowFadeOut,
    Immediate,
};
}

constexpr int MaxPlayers = 4;
inline int g_level_max_y = 126;

struct Player
{
    int uid;
};

struct State
{
    int level;
};

State* get_state()
{
    static State state;
    return &state;
}

void load_libraries(sol::state& lua)
{
    lua.open_libraries(sol::lib::math, sol::lib::base);
}

void load_unsafe_libraries(sol::state& lua)
{
    lua.open_libraries(sol::lib::io);
}

void register_enums(sol::state& lua)
{
    /// Layers of the level.
    lua.create_named_table("LAYER", "FRONT", LAYER::FRONT, "BACK", LAYER::BACK, "PLAYER", -1, "BOTH", -128);

    /// Some constants of the engine
    lua.create_named_table("CONST", "ENGINE_FPS", 60, "MAX_PLAYERS", MaxPlayers * 1, "MAX_TILES_VERT", g_level_max_y, "MASK", 0x10);
    /* CONST
    // ENGINE_FPS
    // The framerate at which the engine updates.
    // Independent of rendering framerate.
    // MAX_PLAYERS
    // Just the max number of players
    // ENGINE_FPS
    // Named twice: these docs win.
    */

    lua.create_named_table("VANILLA_SOUND", "BGM_TITLE", "BGM/BGM_title");

    /// Entity types, filled from the game's database
    lua.create_named_table("ENT_TYPE"
                           //, "FLOOR_BORDERTILE", 1
                           //, "", ...check__[entities.txt]\[game_data/entities.txt\]...
    );
    for (const std::string& name : {"FLOOR_GENERIC", "FLOOR_SURFACE"})
    {
        lua["ENT_TYPE"][name] = 2;
    }

    lua.create_named_table("KEY");
    lua["KEY"]["OL_MOD_CTRL"] = 0x100;
    /// Shift modifier
    lua["KEY"]["OL_MOD_SHIFT"] = 0x200;

    /// Stop modes for an event instance.
    lua.new_enum("FMOD_STOP_MODE", "ALLOW_FADE_OUT", FMODStudio::StopMode::AllowFadeOut, "IMMEDIATE", FMODStudio::StopMode::Immediate);

    // A namespace of functions: not an enum.
    lua.create_named_table("Util");
    lua["Util"]["twice"] = [](int x) -> CallbackId
    { return 2 * x; };

    lua["spawn"] = [](ENT_TYPE type, SHORT_TILE_CODE code, Ids ids) -> Holder<int>
    { return {static_cast<int>(type + code + ids.size())}; };
}

void register_globals(sol::state& lua)
{
    /// The game state
    lua["state"] = get_state();
    /// NoDoc
    lua["P"] = 1;
    lua["__script_id"] = std::string("id");

    /// Table of strings with the script's metadata.
    sol::table meta_table{lua, sol::create};
    lua["meta"] = meta_table;

    Player players[4]{};
    lua["players"] = std::vector<Player>(players, players + 4);

    lua["set_flag"] = [](int flags, int bit)
    { return flags | (1 << bit); };
    /// Deprecated
    lua["setflag"] = lua["set_flag"];
    lua["level"] = [](State& s)
    { return s.level /**/; };
    lua["level_count"] = 16;
}

void run_callbacks(sol::state& lua)
{
    /// Use `set_callback(function, ON.FRAME)` instead
    sol::optional<sol::function> on_frame = lua["on_frame"];
    sol::optional<sol::function> internal = lua["__internal"];
    if (on_frame)
    {
        on_frame.value()();
    }
    if (internal)
    {
        internal.value()();
    }
}
