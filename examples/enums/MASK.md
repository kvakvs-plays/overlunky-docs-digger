**Logical entities have their own mask.** Pass `MASK.LOGICAL` to hook or query invisible logic entities; they are not grid entities, so searches with other masks miss them.

```lua
set_post_entity_spawn(function(ent)
    ent:set_pre_destroy(function(_)
        print("trap trigger destroyed")
    end)
end, SPAWN_TYPE.ANY, MASK.LOGICAL, ENT_TYPE.LOGICAL_ARROW_TRAP_TRIGGER)

local x, y, layer = get_position(players[1].uid)
local found = get_entities_at(ENT_TYPE.LOGICAL_PLATFORM_SPAWNER, MASK.LOGICAL, x, y - 1, layer, 0.1)
print(#found > 0)
```

**Classifying a type by its search flags.** `get_type(id).search_flags` holds the mask bits of an entity type, so you can classify an id, or the `type` of a related entity such as an overlay, without keeping a list.

```lua
local function is_mount(type_id)
    return get_type(type_id).search_flags & MASK.MOUNT ~= 0
end
local holder = players[1].overlay
local carried_by_player = holder ~= nil and holder.type.search_flags & MASK.PLAYER ~= 0
print(is_mount(ENT_TYPE.MOUNT_TURKEY), carried_by_player)
```

**Inspecting the entity just spawned.** Inside a spawn callback, the type of `state.next_entity_uid - 1` can be tested against a mask, for example to see whether a floor was placed in the same cell a moment ago.

```lua
set_pre_entity_spawn(function(_, x, y, _, _, spawn_flags)
    if spawn_flags & SPAWN_TYPE.SCRIPT ~= 0 then return end
    local last = get_entity(state.next_entity_uid - 1)
    if last and last.type.search_flags & MASK.FLOOR ~= 0 and last.x == x and last.y == y then
        print("a floor already sits here")
    end
end, SPAWN_TYPE.LEVEL_GEN_GENERAL, MASK.ITEM)
```

**Filtering by type only.** `MASK.ANY` is 0, meaning no mask filtering: only the listed entity types decide what matches. A concrete mask narrows the hook to that category.

```lua
set_post_entity_spawn(function(ent)
    ent.flags = set_flag(ent.flags, ENT_FLAG.TAKE_NO_DAMAGE)
end, SPAWN_TYPE.ANY, MASK.ANY, ENT_TYPE.MONS_SNAKE, ENT_TYPE.MONS_BAT)
```
