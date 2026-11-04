**It fires after the first level is already generated.** `ON.START` runs on the first frame of the first level of a run, so setup that must influence level generation is too late there. Detect the run start in `ON.PRE_LEVEL_GENERATION` through the reset quest flag instead.

```lua
set_callback(function()
    if test_flag(state.quest_flags, QUEST_FLAG.RESET) then
        print("generating the first level of a new run")
    end
end, ON.PRE_LEVEL_GENERATION)
```
