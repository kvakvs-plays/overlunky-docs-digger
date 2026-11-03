// Bit flag enum tables: values written as `1 << n` (in C++ enumerators or directly), hex masks with a union, decimal
// powers of two, and tables that only look like bits (counting enums with values 0 to 4, too few bits).
#include <sol/sol.hpp>

#include <cstdint>

enum class PAUSE_SCREEN : int64_t
{
    NONE = 0,
    LOGO = 1 << 0,
    INTRO = 1 << 1,
    LOADING = 1 << 30,
    EXIT = 1 << 31,
};

enum class HOTKEY_TYPE : int32_t
{
    NORMAL = 0,
    GLOBAL = 1 << 0,
    INPUT = 1 << 1,
};

enum class PLAYBACK_STATE
{
    PLAYING,
    SUSTAINING,
    STOPPED,
    STARTING,
    STOPPING,
};

void register_flags(sol::state& lua)
{
    /// Used in PauseAPI
    lua.create_named_table("PAUSE_SCREEN", "NONE", PAUSE_SCREEN::NONE, "LOGO", PAUSE_SCREEN::LOGO, "INTRO", PAUSE_SCREEN::INTRO, "LOADING", PAUSE_SCREEN::LOADING, "EXIT", PAUSE_SCREEN::EXIT);
    lua.create_named_table("HOTKEY_TYPE", "NORMAL", HOTKEY_TYPE::NORMAL, "GLOBAL", HOTKEY_TYPE::GLOBAL, "INPUT", HOTKEY_TYPE::INPUT);

    lua.create_named_table("MASK", "ANY", 0x0, "PLAYER", 0x1, "MOUNT", 0x2, "MONSTER", 0x4, "WATER", 0x2000, "LAVA", 0x4000, "LIQUID", 0x6000);
    /* MASK
    // LIQUID
    // Water and lava.
    */
    lua.create_named_table("BUTTON", "JUMP", 1, "WHIP", 2, "BOMB", 4, "ROPE", 8);
    lua.create_named_table("SHIFTED", "A", 1 << 3, "B", 1 << 5);

    // Not flags: counting enums, and only two powers of two without a shift.
    lua.create_named_table("PLAYBACK_STATE", "PLAYING", PLAYBACK_STATE::PLAYING, "SUSTAINING", PLAYBACK_STATE::SUSTAINING, "STOPPED", PLAYBACK_STATE::STOPPED, "STARTING", PLAYBACK_STATE::STARTING, "STOPPING", PLAYBACK_STATE::STOPPING);
    lua.create_named_table("COUNT", "ONE", 1, "TWO", 2, "THREE", 3, "FOUR", 4);
    lua.create_named_table("PAIR", "NONE", 0, "LEFT", 1, "RIGHT", 2);
}
