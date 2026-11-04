**Tear down per-level callbacks when the level is unloaded.** Register the per-level callback and a cleanup callback together; the cleanup clears both, so nothing keeps running into the next screen and the next level starts with one fresh copy.

```lua
set_callback(function()
    local frame_cb = set_callback(function()
        -- behaviour that belongs to this level only
    end, ON.FRAME)
    set_callback(function()
        clear_callback(frame_cb)
        clear_callback() -- this cleanup callback itself
    end, ON.PRE_LEVEL_DESTRUCTION)
end, ON.LEVEL)
```

**`state.screen` is already the next screen.** The screen being torn down is `state.screen_last`, so test that one when the cleanup only applies to a certain screen.

```lua
set_callback(function()
    if state.screen_last == SCREEN.CAMP then
        print("leaving the camp")
    end
end, ON.PRE_LEVEL_DESTRUCTION)
```
