**Run something once, after the next engine update.** A `POST_UPDATE` callback that calls `clear_callback()` runs exactly once, after the game has updated with whatever you just set up. Use it to wait one update after a screen load or a spawn before reading the result.

```lua
set_callback(function()
    set_callback(function()
        clear_callback()
        print("first update of the new screen done, frame", get_frame())
    end, ON.POST_UPDATE)
end, ON.POST_LOAD_SCREEN)
```

**Undo something the engine just did.** Remember the value in `PRE_UPDATE` and write it back in `POST_UPDATE`, so the engine's own change for this update is discarded while everything else it did stays.

```lua
local focus_x
set_callback(function()
    focus_x = state.camera.adjusted_focus_x
end, ON.PRE_UPDATE)
set_callback(function()
    if focus_x then
        state.camera.adjusted_focus_x = focus_x
    end
end, ON.POST_UPDATE)
```

**It does not run when a `PRE_UPDATE` callback blocked the update.** Whenever something returns `true` from `PRE_UPDATE`, `ON.BLOCKED_UPDATE` runs in place of `POST_UPDATE`. Code that must keep running while the engine is frozen belongs there as well.

```lua
set_callback(function()
    print("engine updated")
end, ON.POST_UPDATE)
set_callback(function()
    print("update was blocked")
end, ON.BLOCKED_UPDATE)
```
