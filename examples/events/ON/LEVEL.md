**It only fires in levels.** Not in the camp or the transition screen, and by then generation is done. Register `ON.CAMP` or `ON.TRANSITION` for those, and an earlier event if the change has to affect what gets spawned.

```lua
set_callback(function()
    print("entered a level, monsters: " .. #get_entities_by(0, MASK.MONSTER, LAYER.FRONT))
end, ON.LEVEL)
```
