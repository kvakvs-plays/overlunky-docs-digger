**Logical entities are invisible helpers.** Every `ENT_TYPE.LOGICAL_*` entity belongs to `MASK.LOGICAL` and is not a grid entity, so look for them with the mask-based queries instead of `get_grid_entity_at`.
Pass the type and `MASK.LOGICAL` to `get_entities_at`, `get_entities_by` or `set_post_entity_spawn`.

```lua
local x, y = get_position(players[1].uid)
local near = get_entities_at(ENT_TYPE.LOGICAL_PLATFORM_SPAWNER, MASK.LOGICAL, x, y, LAYER.FRONT, 3)
print(#near, "platform spawners nearby")
```

**Hook a logical entity to learn when something happens.** The arrow trap trigger is destroyed when its trap fires, so a pre-destroy hook on it works as a "trap fired" event.
Use `overlaps_with` against the camera bounds to react only to traps that are on screen.

```lua
set_post_entity_spawn(function(trigger)
    trigger:set_pre_destroy(function(self)
        if state.camera and self:overlaps_with(state.camera:get_bounds()) then
            print("arrow trap fired on screen")
        end
    end)
end, SPAWN_TYPE.ANY, MASK.LOGICAL, ENT_TYPE.LOGICAL_ARROW_TRAP_TRIGGER)
```

**Standalone logicals you can spawn.** `LOGICAL_BOULDERSPAWNER` starts a boulder run on its own and `LOGICAL_ANCHOVY_FLOCK` creates a school of anchovies.

```lua
local x, y = get_position(players[1].uid)
spawn(ENT_TYPE.LOGICAL_BOULDERSPAWNER, x, y, LAYER.FRONT, 0, 0)
spawn_entity(ENT_TYPE.LOGICAL_ANCHOVY_FLOCK, x, y, LAYER.FRONT, 0, 0)
```

**Cutscene bars.** Spawning `LOGICAL_CINEMATIC_ANCHOR` at 0, 0 draws the black cinematic bars.
When the player skips the cutscene the anchor gets `ENT_FLAG.DEAD`, so polling that flag detects the skip. Move scripted actors to their final positions yourself in that case.

```lua
spawn(ENT_TYPE.LOGICAL_CINEMATIC_ANCHOR, 0, 0, LAYER.FRONT, 0, 0)
state.pause = PAUSE_TYPE.CUTSCENE
-- later, e.g. in ON.POST_UPDATE:
local anchor = get_entity(get_entities_by_type(ENT_TYPE.LOGICAL_CINEMATIC_ANCHOR)[1] or -1)
if anchor and test_flag(anchor.flags, ENT_FLAG.DEAD) then
    print("cutscene skipped")
end
```

**Liquid falls are drain logicals.** `LOGICAL_WATER_DRAIN` and `LOGICAL_LAVA_DRAIN` are themselves the drawn drain tile (retexturable, `animation_frame` picks the sprite), and each owns a `BG_WATER_FOUNTAIN` item for the fountain head.
The entity type decides between water and lava.

```lua
local x, y = get_position(players[1].uid)
local drain = get_entity(spawn_entity(ENT_TYPE.LOGICAL_LAVA_DRAIN, x, y + 2, LAYER.FRONT, 0, 0))
local heads = entity_get_items_by(drain.uid, ENT_TYPE.BG_WATER_FOUNTAIN, 0)
print(drain.uid, "has", #heads, "fountain head(s)")
```

**A persistent light from LOGICAL_ROOM_LIGHT.** Spawning one over an entity gives a ready `Illumination`; make it follow the target and set the colour and size.

```lua
local target = players[1].uid
local room_light = get_entity(spawn_over(ENT_TYPE.LOGICAL_ROOM_LIGHT, target, 0, 0))
local ill = room_light.illumination
ill.type_flags = LIGHT_TYPE.FOLLOW_ENTITY
ill.entity_uid = target
ill.light1.red, ill.light1.green, ill.light1.blue, ill.light1.size = 0.2, 0.4, 1, 2
```

**Helpers sit next to their owner.** A monster that owns a logical helper has it at the next uid when you spawn it from script, e.g. the `LOGICAL_FROST_BREATH` of `MONS_AMMIT`.
Check the type before touching it, then move the effect origin with `special_offsetx` and `special_offsety`.

```lua
local x, y = get_position(players[1].uid)
local mon = spawn(ENT_TYPE.MONS_AMMIT, x + 2, y, LAYER.FRONT, 0, 0)
local breath = get_entity(mon + 1)
if breath and breath.type.id == ENT_TYPE.LOGICAL_FROST_BREATH then
    breath.special_offsetx, breath.special_offsety = 0.7, -0.7
end
```

**Replacing a spawn leaves its logical behind.** In a `set_pre_entity_spawn` callback the logical entity the game created just before the floor is `state.next_entity_uid - 1`; destroy it if you replace the floor, or it stays as a stray.
A harmless entity such as `FX_SHADOW` works as a stand-in to return. Skip `SPAWN_TYPE.SCRIPT` spawns to ignore your own.
A working door is several entities (floor, background, logicals), so read the `Door` type page before replacing one.

```lua
set_pre_entity_spawn(function(_, x, y, layer, _, flags)
    if flags & SPAWN_TYPE.SCRIPT ~= 0 then return end
    local prev = get_entity(state.next_entity_uid - 1)
    if prev and prev.type.id == ENT_TYPE.LOGICAL_DOOR then prev:destroy() end
    return spawn_grid_entity(ENT_TYPE.FX_SHADOW, x, y, layer)
end, SPAWN_TYPE.LEVEL_GEN_GENERAL, MASK.FLOOR, ENT_TYPE.FLOOR_DOOR_COG)
```
