**Release what the game does not own for you.** Your callbacks stop with the script, but something you started on the side, like a looping sound, may keep going. This also runs right before a reload, so a reloaded script does not stack a second copy.

```lua
local music = create_sound("music/loop.ogg")
local playing = music and music:play()
if playing then playing:set_looping(SOUND_LOOP_MODE.LOOP) end
set_callback(function()
    if playing then playing:stop() end
end, ON.SCRIPT_DISABLE)
```
