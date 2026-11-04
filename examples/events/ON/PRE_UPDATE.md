**Write scripted inputs here so the update sees them.** Overwrite the player slot's buttons in `PRE_UPDATE` and the engine reads your values in the update that follows. Set `buttons` and `buttons_gameplay` together, and keep the callback id so you can stop scripting when the level goes away.

```lua
local cb = set_callback(function()
    local slot = state.player_inputs.player_slots[1]
    slot.buttons = INPUTS.NONE
    slot.buttons_gameplay = INPUTS.NONE
end, ON.PRE_UPDATE)
set_callback(function()
    clear_callback(cb)
    clear_callback()
end, ON.PRE_LEVEL_DESTRUCTION)
```

**Return `true` only while you want the freeze.** Returning nothing lets the update run; returning `true` skips it, and with it `POST_UPDATE` (`ON.BLOCKED_UPDATE` runs instead). Count down your own timer in `PRE_UPDATE`, because anything counted in `POST_UPDATE` stops with the engine.

```lua
local frames = 60
set_callback(function()
    if frames > 0 then
        frames = frames - 1
        return true
    end
end, ON.PRE_UPDATE)
```
