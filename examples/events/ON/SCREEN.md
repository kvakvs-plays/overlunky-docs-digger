**Scope a callback to the screen it was created on.** Remember `state.screen` when you register the work, and in `ON.SCREEN` remove everything once the screen differs. `clear_callback()` without arguments removes the callback that is running.

```lua
local initial_screen = state.screen
local draw_cb = set_callback(function(_)
    -- per-frame work that only makes sense on this screen
end, ON.GUIFRAME)
set_callback(function()
    if state.screen ~= initial_screen then
        clear_callback(draw_cb)
        clear_callback()
    end
end, ON.SCREEN)
```
