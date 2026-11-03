**There is no cancel, return a stand-in.** The callback can only return a uid, so to drop a vanilla spawn, spawn something inert at the same place and return its uid. One callback can cover several types.

```lua
set_pre_entity_spawn(function(_, x, y, layer, _, spawn_flags)
    if spawn_flags & SPAWN_TYPE.SCRIPT ~= 0 then return end -- keep your own spawns
    return spawn_entity(ENT_TYPE.FX_SHADOW, x, y, layer, 0, 0)
end, SPAWN_TYPE.LEVEL_GEN_PROCEDURAL, MASK.MONSTER, ENT_TYPE.MONS_BAT, ENT_TYPE.MONS_SPIDER)
```

**Keep what is already there.** Returning the uid of an existing entity makes the game keep the occupant instead of spawning a new one. `get_grid_entity_at` returns -1 for an empty cell, so return only otherwise.

```lua
set_pre_entity_spawn(function(_, x, y, layer, _, spawn_flags)
    if spawn_flags & SPAWN_TYPE.SCRIPT ~= 0 then return end
    local uid_at = get_grid_entity_at(x, y, layer)
    if uid_at ~= -1 then return uid_at end
end, SPAWN_TYPE.LEVEL_GEN_GENERAL, MASK.FLOOR, ENT_TYPE.FLOOR_VINE_TREE_TOP)
```

**Use spawn_entity_nonreplaceable for the replacement.** A plain `spawn` goes through the pre-spawn chain again, so a replacement of the same type is replaced again, by this or another script. The nonreplaceable variant skips the callbacks.

```lua
local pool = { ENT_TYPE.ITEM_PICKUP_ROPEPILE, ENT_TYPE.ITEM_PICKUP_BOMBBAG }
set_pre_entity_spawn(function(_, x, y, layer)
    return spawn_entity_nonreplaceable(pool[math.random(#pool)], x, y, layer, 0, 0)
end, SPAWN_TYPE.LEVEL_GEN, MASK.ITEM, ENT_TYPE.ITEM_PICKUP_ROPEPILE)
```

**Pair a floor swap with a post-spawn fix.** When the replacement is a different floor type, decorations and neighbours are not updated for you, so add a `set_post_entity_spawn` on the original type that calls `fix_decorations`.

```lua
set_pre_entity_spawn(function(_, x, y, layer)
    return spawn_grid_entity(ENT_TYPE.FLOOR_BORDERTILE_METAL, x, y, layer)
end, SPAWN_TYPE.ANY, MASK.ANY, ENT_TYPE.FLOOR_HORIZONTAL_FORCEFIELD)
set_post_entity_spawn(function(floor)
    floor:fix_decorations(true, true)
end, SPAWN_TYPE.ANY, MASK.ANY, ENT_TYPE.FLOOR_HORIZONTAL_FORCEFIELD)
```

**Replacing runtime drops.** Items that appear during play are `SYSTEMIC`, not level generation, so register with that type to replace crate or altar contents.

```lua
set_pre_entity_spawn(function(_, x, y, layer)
    return spawn_entity_nonreplaceable(ENT_TYPE.ITEM_ROCK, x, y, layer, 0, 0)
end, SPAWN_TYPE.SYSTEMIC, MASK.ITEM, ENT_TYPE.ITEM_PICKUP_BOMBBAG)
```
