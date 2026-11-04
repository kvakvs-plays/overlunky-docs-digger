**Reset per-level state here, not in `ON.LEVEL`.** No entities exist yet, but the spawn callbacks fire during generation and fill your tables. Clearing in `ON.LEVEL` would throw away what they collected for this level.

```lua
local marked = {}
set_post_entity_spawn(function(ent)
    marked[ent.uid] = true
end, SPAWN_TYPE.LEVEL_GEN_PROCEDURAL, MASK.ITEM)

set_callback(function()
    marked = {}
end, ON.PRE_LEVEL_GENERATION)

set_callback(function()
    local count = 0
    for _ in pairs(marked) do count = count + 1 end
    print("items placed by level generation: " .. count)
end, ON.POST_LEVEL_GENERATION)
```

**Use `clear_callback()` for a one-shot "next level" hook.** Called without arguments inside a callback it unregisters that callback, so the work runs exactly once, when the next level starts generating.

```lua
set_callback(function()
    print("the next level is about to generate")
    clear_callback()
end, ON.PRE_LEVEL_GENERATION)
```
