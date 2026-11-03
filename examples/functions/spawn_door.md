**What `spawn_door` makes.** It spawns an exit with its target already set (`special_door`), rounded to the grid, and
a `LOGICAL_PLATFORM_SPAWNER` below it. It needs a level or the camp as `state.screen_next`: there it spawns a
`FLOOR_DOOR_EXIT`, in the camp a `FLOOR_DOOR_STARTING_EXIT`; on any other screen there is no door to make and the call
fails. It does not spawn the door art: without a `BG_DOOR` the exit works but is invisible.

```lua
-- A visible door two tiles right of the first player, leading to 1-2.
local px, py, layer = get_position(players[1].uid)
local x, y = math.floor(px + 0.5) + 2, math.floor(py + 0.5)
spawn_door(x, y, layer, 1, 2, THEME.DWELLING)
local art = get_entity(spawn_entity(ENT_TYPE.BG_DOOR, x, y + 0.31, layer, 0, 0))
art.animation_frame = 1 -- 0 shows the door closed
```

**A door that opens later.** Spawn the art with its closed frame and remove the enter prompt; `Door:unlock(true)` later
brings back both the prompt and the open frame (it finds the `BG_DOOR` in the door's grid cell).

```lua
local px, py, layer = get_position(players[1].uid)
local x, y = math.floor(px + 0.5) + 2, math.floor(py + 0.5)
local door = get_entity(spawn_door(x, y, layer, state.world, state.level + 1, state.theme))
spawn_entity(ENT_TYPE.BG_DOOR, x, y + 0.31, layer, 0, 0)
door:unlock(false)
set_timeout(function() door:unlock(true) end, 180)
```
