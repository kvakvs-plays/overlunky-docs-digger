**Script spawns re-enter the callbacks.** Entities created by Lua, including your own script, also run spawn callbacks. A callback that replaces or removes spawns should return early when `SPAWN_TYPE.SCRIPT` is set, otherwise it eats the entities you create on purpose.

```lua
set_pre_entity_spawn(function(_, x, y, layer, _, spawn_flags)
    if spawn_flags & SPAWN_TYPE.SCRIPT ~= 0 then return end
    return spawn_entity(ENT_TYPE.FX_SHADOW, x, y, layer, 0, 0) -- inert stand-in
end, SPAWN_TYPE.LEVEL_GEN_PROCEDURAL, MASK.MONSTER, ENT_TYPE.MONS_BAT)
```

**Pick the narrowest type.** Registering for `LEVEL_GEN_TILE_CODE` only sees tile code spawns, `LEVEL_GEN_PROCEDURAL` the random monsters and items, `LEVEL_GEN_FLOOR_SPREADING` styled floor bleeding into generic floor, and `SYSTEMIC` runtime spawns. Combine values with `|`.

```lua
set_post_entity_spawn(function(ent, spawn_flags)
    if spawn_flags & SPAWN_TYPE.LEVEL_GEN_TILE_CODE ~= 0 then
        print("placed by a tile code", ent.uid)
    else
        print("random or runtime spawn", ent.uid)
    end
end, SPAWN_TYPE.LEVEL_GEN_TILE_CODE | SPAWN_TYPE.LEVEL_GEN_PROCEDURAL, MASK.MONSTER, ENT_TYPE.MONS_SNAKE)
```

**Replacing a floor that spreads during generation.** Spread floor is its own spawn type, so you can restyle it without touching floors placed by tile codes.

```lua
set_pre_entity_spawn(function(_, x, y, layer, _, spawn_flags)
    if spawn_flags & SPAWN_TYPE.SCRIPT ~= 0 then return end
    return spawn_grid_entity(ENT_TYPE.FLOORSTYLED_GUTS, x, y, layer)
end, SPAWN_TYPE.LEVEL_GEN_FLOOR_SPREADING, MASK.FLOOR, ENT_TYPE.FLOOR_GENERIC)
```

**Runtime drops are SYSTEMIC.** Contents of crates, altar rewards and thrown bombs are not level generation. Replace them by returning a new uid from a `SYSTEMIC` pre-spawn callback. The position is fractional, so test both the floor and the ceil cell when looking for the tile below.

```lua
set_pre_entity_spawn(function(_, x, y, layer)
    for _, cx in ipairs({ math.floor(x), math.ceil(x) }) do
        if get_entity_type(get_grid_entity_at(cx, y - 1, layer)) == ENT_TYPE.FLOOR_ALTAR then
            return spawn_entity_nonreplaceable(ENT_TYPE.ITEM_JETPACK, x, y, layer, 0, 0)
        end
    end
end, SPAWN_TYPE.SYSTEMIC, MASK.ITEM, ENT_TYPE.ITEM_PICKUP_SKELETON_KEY)
```
