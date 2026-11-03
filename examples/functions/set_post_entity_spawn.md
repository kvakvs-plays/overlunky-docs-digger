**Find the entity the game just spawned.** In a tile code callback or a spawn callback, `state.next_entity_uid - 1` is the entity spawned right before. Companions such as a shopkeeper's weapon can come after the one you want, so walk back a few uids.

```lua
set_post_tile_code_callback(function(_, _, _)
    local uid = state.next_entity_uid - 1
    for _ = 1, 4 do
        if get_entity_type(uid) == ENT_TYPE.MONS_SHOPKEEPER then break end
        uid = uid - 1
    end
    local keeper = get_entity(uid)
    if keeper then keeper.has_key = false end
end, "shopkeeper")
```

**Guard against your own spawns.** `SPAWN_TYPE.ANY` also matches spawns made by your script. If the hook must skip your own variant of the same type, set a local around your `spawn` call.

```lua
local spawning_own = false
set_post_entity_spawn(function(ent)
    if spawning_own then return end
    ent.flags = set_flag(ent.flags, ENT_FLAG.TAKE_NO_DAMAGE)
end, SPAWN_TYPE.ANY, MASK.ANY, ENT_TYPE.MONS_SNAKE)

local x, y, layer = get_position(players[1].uid)
spawning_own = true
spawn(ENT_TYPE.MONS_SNAKE, x + 2, y, layer, 0, 0)
spawning_own = false
```

**Temporary callback around one action.** To change the by-products of a single action, register a callback for the exact type, do the action synchronously, then clear it with the returned id.

```lua
local floor = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1] or -1)
if floor then
    local cb = set_post_entity_spawn(function(rubble)
        rubble.animation_frame = 40
    end, SPAWN_TYPE.ANY, MASK.FX, ENT_TYPE.ITEM_RUBBLE)
    floor:kill(false, nil)
    clear_callback(cb)
end
```
