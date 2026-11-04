**It runs for every level load, so guard on the screen and theme.** Only force a theme or add level files when the current load is the one you target, or the change leaks into every other level. `state.screen` and `state.theme` already describe the level being loaded. `add_level_files` keeps the game's own files for that level, `override_level_files` replaces them.

```lua
set_callback(function(ctx)
    if state.screen ~= SCREEN.LEVEL or state.theme ~= THEME.DWELLING then return end
    ctx:add_level_files({ "my_extra_rooms.lvl" })
end, ON.PRE_LOAD_LEVEL_FILES)
```
