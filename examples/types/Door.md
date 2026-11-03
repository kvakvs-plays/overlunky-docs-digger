**A working door is several entities.** The `FLOOR_DOOR_*` entity is only the part a player enters; it draws nothing.
The door art is a separate `BG_DOOR` centred 0.31 tiles above the door's cell (`animation_frame` 0 shows it closed, 1
open), and a `LOGICAL_PLATFORM_SPAWNER` one tile below makes the floor under the door. Vanilla exits also carry an
`FX_COMPASS`, the arrow the Compass shows. Level generation spawns all of these from the door tile code; a script that
builds a door by hand has to spawn each part.

```lua
-- Build a complete exit door two tiles right of the first player, leading to 1-2.
local px, py, layer = get_position(players[1].uid)
local x, y = math.floor(px + 0.5) + 2, math.floor(py + 0.5)
local exit_uid = spawn_entity(ENT_TYPE.FLOOR_DOOR_EXIT, x, y, layer, 0, 0)
spawn_entity_over(ENT_TYPE.FX_COMPASS, exit_uid, 0, 0)
spawn_entity(ENT_TYPE.LOGICAL_PLATFORM_SPAWNER, x, y - 1, layer, 0, 0)
local art = get_entity(spawn_entity(ENT_TYPE.BG_DOOR, x, y + 0.31, layer, 0, 0))
art.animation_frame = 1
set_door_target(exit_uid, 1, 2, THEME.DWELLING)
```

**Where an exit leads.** `set_door_target` stores the world, level and theme in the `ExitDoor` and sets its
`special_door`. An exit without it behaves like a vanilla one and goes to `state.world_next`, `level_next` and
`theme_next`; `get_door_target` returns nothing for such a door, while `ExitDoor:get_target()` returns those values.

```lua
local door = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_DOOR_EXIT)[1])
print(door.special_door, door:get_target())
```

**Locking and unlocking.** For entrances and exits, `door:unlock(false)` clears `ENT_FLAG.ENABLE_BUTTON_PROMPT` and turns
the `BG_DOOR` in the same grid cell to its closed frame; `unlock(true)` reverses both. It leaves `ENT_FLAG.LOCKED` alone,
which the deprecated `lock_door_at` sets as well. A door locked by flags (prompt cleared, `LOCKED` set) needs both flags
put back when it opens, or the enter prompt does not come back.

```lua
-- Keep the exit shut for five seconds.
local function set_open(door, open)
    door:unlock(open) -- button prompt and the open or closed art
    door.flags = open and clr_flag(door.flags, ENT_FLAG.LOCKED) or set_flag(door.flags, ENT_FLAG.LOCKED)
end
local exit = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_DOOR_EXIT)[1])
set_open(exit, false)
set_timeout(function() set_open(exit, true) end, 300)
```

**Custom door art.** Retexture the `BG_DOOR`, not the floor door. A replacement sheet holds the closed door on top and
the open door below it, so the texture is two tiles high: `height = 2 * tile_height`.

```lua
local def = TextureDefinition.new()
def.texture_path, def.width, def.height, def.tile_width, def.tile_height = "my_door.png", 386, 640, 386, 320
local tex = define_texture(def)
for _, uid in ipairs(get_entities_by_type(ENT_TYPE.BG_DOOR)) do get_entity(uid):set_texture(tex) end
```

**The ambient sound at an exit.** The hum of the next area near an exit comes from a `LOGICAL_DOOR_AMBIENT_SOUND`
attached to the door as an item. It has to be spawned with an overlay (`spawn_over`); to move one to another door,
`move_entity` it there and `attach_entity` it.

```lua
local doors = get_entities_by(ENT_TYPE.FLOOR_DOOR_EXIT, MASK.FLOOR, LAYER.FRONT)
local sound = entity_get_items_by(doors[1], ENT_TYPE.LOGICAL_DOOR_AMBIENT_SOUND, MASK.LOGICAL)[1]
if #doors > 1 and sound then
    local x, y = get_position(doors[2])
    move_entity(sound, x, y, 0, 0)
    attach_entity(doors[2], sound)
end
```

**Telling that a player is entering a door.** While entering, the player overlaps the door and its `state` is
`CHAR_STATE.ENTERING`. A player on a mount does not get that state; the mount (`player.overlay`) does. The
`set_pre_enter`/`set_post_enter` hooks of a door are the alternative when a callback per door is enough.

```lua
local function entering(door, ent)
    return door:overlaps_with(ent) and ent.state == CHAR_STATE.ENTERING
end
local function player_entering(door, player)
    local mount = player.overlay
    local on_mount = mount ~= nil and mount.type.search_flags & MASK.MOUNT ~= 0
    return entering(door, player) or (on_mount and entering(door, mount))
end
set_callback(function()
    local exit = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_DOOR_EXIT)[1] or -1)
    if exit and player_entering(exit, players[1]) then print("entering the exit") end
end, ON.FRAME)
```

**The doors of the transition room.** On `SCREEN.TRANSITION` the front layer has two `BG_DOOR`s: the first belongs to
the level just left (`state.theme`), the second to the next one (`state.theme_next`). Restyle them in
`ON.POST_LEVEL_GENERATION`.

```lua
set_callback(function()
    if state.screen ~= SCREEN.TRANSITION or state.theme_next ~= THEME.TEMPLE then return end
    local doors = get_entities_by(ENT_TYPE.BG_DOOR, MASK.BG, LAYER.FRONT)
    get_entity(doors[2]):set_texture(TEXTURE.DATA_TEXTURES_FLOOR_TEMPLE_1) -- the door towards the next level
end, ON.POST_LEVEL_GENERATION)
```

**Replacing the doors of the level files.** A door tile code spawns the floor door, its `BG_DOOR`, the
`LOGICAL_PLATFORM_SPAWNER` and (for an entrance) a `LOGICAL_DOOR`. To build your own door instead, kill all of them as
they spawn from the tile code; the `LEVEL_GEN_TILE_CODE` filter leaves the parts your script spawns alone.

```lua
local function remove(ent) if state.screen == SCREEN.LEVEL then kill_entity(ent.uid) end end
for _, t in ipairs({ ENT_TYPE.FLOOR_DOOR_ENTRANCE, ENT_TYPE.BG_DOOR, ENT_TYPE.LOGICAL_DOOR,
    ENT_TYPE.LOGICAL_PLATFORM_SPAWNER }) do
    set_post_entity_spawn(remove, SPAWN_TYPE.LEVEL_GEN_TILE_CODE, MASK.ANY, t)
end
```

**A hidden door to the back layer.** A layer door needs a `LOGICAL_PLATFORM_SPAWNER` below it in both layers; its art
in the front layer is a `BG_DOOR_FRONT_LAYER` at the door's own position. Spawning the `FLOOR_DOOR_LAYER` only when the
floor in front of it is destroyed gives a secret passage.

```lua
local x, y = 10, 100 -- a floor tile in the front layer
spawn_entity(ENT_TYPE.LOGICAL_PLATFORM_SPAWNER, x, y - 1, LAYER.FRONT, 0, 0)
spawn_entity(ENT_TYPE.LOGICAL_PLATFORM_SPAWNER, x, y - 1, LAYER.BACK, 0, 0)
spawn_entity(ENT_TYPE.BG_DOOR_FRONT_LAYER, x, y, LAYER.FRONT, 0, 0)
get_entity(get_grid_entity_at(x, y, LAYER.FRONT)):set_post_destroy(function()
    spawn_entity(ENT_TYPE.FLOOR_DOOR_LAYER, x, y, LAYER.FRONT, 0, 0)
end)
```
