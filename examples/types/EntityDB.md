**A clone keeps the original `id`, and `id` is writable.** Code that checks `entity.type.id` keeps working on cloned entities. Assigning another `ENT_TYPE` to the clone's `id` makes id-based game checks (for example rope interaction) treat the entity as that other type while it keeps the cloned stats.

```lua
local clone = EntityDB:new(ENT_TYPE.MONS_ANUBIS2)
print(clone.id == ENT_TYPE.MONS_ANUBIS2) -- true
clone.id = ENT_TYPE.MONS_ANUBIS          -- id-based checks now see a plain Anubis
set_post_entity_spawn(function(ent)
    ent.type = clone
end, SPAWN_TYPE.ANY, MASK.MONSTER, ENT_TYPE.MONS_ANUBIS2)
```

**Edits through `get_type` are global.** The table returned by `get_type` is the shared type, so a change affects every existing and future entity of that type, including ones spawned by the game. Clone the type and assign it to `entity.type` when only some entities should change.

```lua
local db = get_type(ENT_TYPE.MONS_HERMITCRAB)
db.life = 2                      -- all hermit crabs, now and later
db.leaves_corpse_behind = false
```

**Restore a temporary edit one frame later.** `default_flags` and `draw_depth` are applied when an entity spawns, so set them on the shared type, spawn, and restore at once to get a one-off entity. Death sounds are read at kill time, so set them to -1 and restore with `set_timeout(..., 1)`; another kill of that type on the same frame is silent too.

```lua
local db = get_type(ENT_TYPE.MONS_TIKIMAN)
local old = db.sound_killed_by_player
db.sound_killed_by_player = -1 -- mute the death sound of this kill
set_timeout(function()
    db.sound_killed_by_player = old
end, 1)
```

**Swap `entity.type` at runtime to change physics per state.** Friction, weight and speed live on the type, not the entity. Keep a clone with the changed value and a clone of the original, and assign whichever the current state needs; clone before editing so the second copy keeps the vanilla values.

```lua
local grip = EntityDB:new(ENT_TYPE.MONS_FROG)    -- keep both in long-lived locals
local slide = EntityDB:new(grip)
slide.friction = 0.0
set_post_entity_spawn(function(ent)
    ent:set_post_update_state_machine(function(self)
        self.type = self.frozen_timer > 0 and grip or slide -- frozen things slide normally
    end)
end, SPAWN_TYPE.ANY, MASK.MONSTER, ENT_TYPE.MONS_FROG)
```

**Size fields are spawn defaults and are not copied to the entity.** After assigning a clone with a different size, copy `width`, `height`, `hitboxx` and `hitboxy` (and `offsety` if you changed it) to the entity yourself. `damage` is read from the type, so it needs no copy.

```lua
local small = EntityDB:new(ENT_TYPE.ITEM_HUNDUN_FIREBALL)
small.damage, small.width, small.height, small.hitboxx, small.hitboxy = 1, 1.08, 1.08, 0.25, 0.25
local x, y, layer = get_position(players[1].uid)
local fireball = get_entity(spawn(ENT_TYPE.ITEM_HUNDUN_FIREBALL, x, y, layer, 0.1, 0))
fireball.type = small
fireball.width, fireball.height = small.width, small.height
fireball.hitboxx, fireball.hitboxy = small.hitboxx, small.hitboxy
```

**A movement profile goes on one clone.** Friction fights a script that writes `velocityx` every frame, so use `friction = 0` for such monsters. A very low `weight` stops spring traps from launching a heavy monster.

```lua
local heavy = EntityDB:new(ENT_TYPE.MONS_AMMIT) -- keep in a long-lived local
heavy.friction = 0     -- the script drives velocityx
heavy.weight = 0.2     -- springs do not launch it
heavy.max_speed = 0.05
```

**Stack clones; `sacrifice_value` is per type.** `EntityDB:new` accepts an `ENT_TYPE` or an existing `EntityDB`, including a previous clone, so variants inherit earlier edits. A monster worth a different altar favour needs its own clone.

```lua
local base = EntityDB:new(ENT_TYPE.MONS_SCORPION)
base.sacrifice_value = 6
local flying = EntityDB:new(base) -- inherits sacrifice_value
flying.friction = 0.0
```

**Test a category through `search_flags`.** `get_type(id).search_flags` holds the `MASK` bits of a type, so you can classify an entity from just its type id: a container's `inside`, an overlay, or the entity spawned last.

```lua
local function is_mount(type_id)
    return get_type(type_id).search_flags & MASK.MOUNT ~= 0
end
local last = get_entity_type(state.next_entity_uid - 1) -- the entity spawned most recently
print(is_mount(last))
```
