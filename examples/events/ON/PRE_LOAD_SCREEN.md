**`state.screen` is the screen you are leaving, `state.screen_next` the one about to load.** Check both to react to one specific transition, and assign `state.screen_next` to redirect it. Here the camp loads after the credits instead of the screen the game picked.

```lua
set_callback(function()
    if state.screen == SCREEN.CREDITS then
        state.screen_next = SCREEN.CAMP
    end
end, ON.PRE_LOAD_SCREEN)
```

**Reset per-run data only on a real new run.** The same transition happens when you come back from the options menu, so look at the quest flags and the screen you are leaving before wiping anything.

```lua
local run_data = { kills = 0 }
set_callback(function()
    if state.screen_next == SCREEN.LEVEL and state.screen ~= SCREEN.OPTIONS
        and test_flag(state.quest_flags, QUEST_FLAG.RESET) then
        run_data = { kills = 0 }
        print("reset, kills:", run_data.kills)
    end
end, ON.PRE_LOAD_SCREEN)
```

**Return `true` only to cancel the load, and only conditionally.** A callback that returns `true` stops the screen from loading and the remaining `PRE_LOAD_SCREEN` callbacks of your script are skipped for that attempt. Returning nothing, `nil` or `false` lets the load go on, so never return `true` unconditionally.

```lua
local hold_back = false -- set elsewhere, e.g. while your own cutscene plays
set_callback(function()
    if hold_back and state.screen_next == SCREEN.LEVEL then
        return true
    end
end, ON.PRE_LOAD_SCREEN)
```
