**Fix decorations after a floor swap.** When a floor is replaced in a pre-spawn callback, call `fix_decorations(true, true)` from a post-spawn callback on the original type, so the neighbours and styled floor get their edges.

```lua
set_pre_entity_spawn(function(_, x, y, layer)
    return spawn_grid_entity(ENT_TYPE.FLOOR_BORDERTILE_METAL, x, y, layer)
end, SPAWN_TYPE.ANY, MASK.ANY, ENT_TYPE.FLOOR_HORIZONTAL_FORCEFIELD)
set_post_entity_spawn(function(floor)
    floor:fix_decorations(true, true)
end, SPAWN_TYPE.ANY, MASK.ANY, ENT_TYPE.FLOOR_HORIZONTAL_FORCEFIELD)
```

**Floors spawned outside level generation.** A floor spawned after generation is not decorated automatically. Call `fix_decorations` on it, with neighbours, once it exists.

```lua
local x, y, layer = get_position(players[1].uid)
local floor = get_entity(spawn_grid_entity(ENT_TYPE.FLOOR_GENERIC, x + 2, y, layer))
floor:fix_decorations(true, false)
```

**Styled floors: decorate one frame later.** After removing a floor and spawning a `FLOORSTYLED_*` one in a loaded level, call `decorate_internal` after a one frame timeout so the neighbours exist. When changing many tiles, decorate only after all are spawned.

```lua
for _, uid in ipairs(get_entities_by(ENT_TYPE.FLOOR_GENERIC, MASK.FLOOR, LAYER.FRONT)) do
    local x, y, layer = get_position(uid)
    get_entity(uid):remove()
    local floor = get_entity(spawn_on_floor(ENT_TYPE.FLOORSTYLED_GUTS, x, y, layer))
    set_global_timeout(function() floor:decorate_internal() end, 1)
end
```

**Repair the neighbours after removing a floor.** `destroy_grid` leaves the decorations of the eight surrounding tiles stale. Fix the normal floors with `fix_decorations`, and the styled ones with `decorate_internal`.

```lua
local x, y, layer = get_position(players[1].uid)
x, y = math.floor(x + 0.5), math.floor(y + 0.5) - 1
destroy_grid(x, y, layer)
for dx = -1, 1 do
    for dy = -1, 1 do
        local uid = get_grid_entity_at(x + dx, y + dy, layer)
        local floor = (dx ~= 0 or dy ~= 0) and uid ~= -1 and get_entity(uid) or nil
        if floor and floor.type.id >= ENT_TYPE.FLOORSTYLED_MINEWOOD then -- FLOORSTYLED_* ids come last
            floor:decorate_internal()
        elseif floor then
            floor:fix_decorations(false, true)
        end
    end
end
```
