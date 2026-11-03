**Scripted mover that ignores physics.** To turn an item into a piece you position yourself, set `NO_GRAVITY` and clear the flags that make it interact with the world. Write the position every frame from a post-update hook; adding the anchor's velocity avoids lagging one frame behind a moving parent.

```lua
local anchor = players[1]
local x, y, layer = get_position(anchor.uid)
local piece = get_entity(spawn(ENT_TYPE.ITEM_ROCK, x, y, layer, 0, 0))
piece.flags = set_flag(piece.flags, ENT_FLAG.NO_GRAVITY)
for _, f in ipairs({ ENT_FLAG.COLLIDES_WALLS, ENT_FLAG.INTERACT_WITH_SEMISOLIDS,
    ENT_FLAG.INTERACT_WITH_WATER, ENT_FLAG.INTERACT_WITH_WEBS, ENT_FLAG.CLIMBABLE }) do
    piece.flags = clr_flag(piece.flags, f)
end
piece:set_post_update_state_machine(function(self)
    self.x, self.y = anchor.x + anchor.velocityx, anchor.y + 1 + anchor.velocityy
end)
```

**Invisible, untouchable helper.** Combine `INVISIBLE`, `TAKE_NO_DAMAGE`, `PASSES_THROUGH_OBJECTS` and `PASSES_THROUGH_EVERYTHING` for an entity that must exist (for its sounds or as a prop) but never be seen or hit.

```lua
local x, y, layer = get_position(players[1].uid)
local ghost = get_entity(spawn(ENT_TYPE.MONS_BAT, x, y, layer, 0, 0))
for _, f in ipairs({ ENT_FLAG.INVISIBLE, ENT_FLAG.TAKE_NO_DAMAGE,
    ENT_FLAG.PASSES_THROUGH_OBJECTS, ENT_FLAG.PASSES_THROUGH_EVERYTHING }) do
    ghost.flags = set_flag(ghost.flags, f)
end
```

**Harmless spawn-in.** `PASSES_THROUGH_PLAYER` plus `PASSES_THROUGH_EVERYTHING` lets a monster appear without touching anyone. Clear both when it should become dangerous.

```lua
local x, y, layer = get_position(players[1].uid)
local monster = get_entity(spawn(ENT_TYPE.MONS_SNAKE, x + 2, y, layer, 0, 0))
local ghostly = { ENT_FLAG.PASSES_THROUGH_PLAYER, ENT_FLAG.PASSES_THROUGH_EVERYTHING }
for _, f in ipairs(ghostly) do monster.flags = set_flag(monster.flags, f) end
set_global_timeout(function()
    for _, f in ipairs(ghostly) do monster.flags = clr_flag(monster.flags, f) end
end, 60)
```

**Forcing the facing direction.** Facing is just the `FACING_LEFT` flag, and the AI rewrites it, so set it again on every update. Assign it from a boolean with the `and`/`or` idiom.

```lua
local target = players[1]
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1] or -1)
if monster then
    monster:set_post_update_state_machine(function(self)
        if target.x < self.x then
            self.flags = set_flag(self.flags, ENT_FLAG.FACING_LEFT)
        else
            self.flags = clr_flag(self.flags, ENT_FLAG.FACING_LEFT)
        end
    end)
end
```

**Fake corpse.** Setting `health` to 0 and the `DEAD` flag gives a corpse-like entity that stays in the level without running death logic. Move `state` to the dying state yourself.

```lua
local npc = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1] or -1)
if npc then
    npc.health = 0
    npc.last_state, npc.state = npc.state, CHAR_STATE.DYING
    npc.flags = set_flag(npc.flags, ENT_FLAG.DEAD)
end
```

**Floors in a shop zone.** A floor you spawn to replace shop walls should carry `SHOP_FLOOR`, so code that tests for it (including your own placement checks) treats the tile as part of the shop. `INDESTRUCTIBLE_OR_SPECIAL_FLOOR` is the flag to test when deciding whether a solid tile may be destroyed.

```lua
set_pre_tile_code_callback(function(x, y, layer)
    local floor = get_entity(spawn_grid_entity(ENT_TYPE.FLOORSTYLED_TEMPLE, x, y, layer))
    floor.flags = set_flag(floor.flags, ENT_FLAG.SHOP_FLOOR)
    return true -- the vanilla floor is not spawned
end, "shop_wall")

local function can_destroy(floor)
    return test_flag(floor.flags, ENT_FLAG.SOLID)
        and not test_flag(floor.flags, ENT_FLAG.INDESTRUCTIBLE_OR_SPECIAL_FLOOR)
end
print(can_destroy(get_entity(get_grid_entity_at(0, 0, LAYER.FRONT))))
```
