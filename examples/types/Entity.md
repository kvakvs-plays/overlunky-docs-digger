**`x`/`y` are relative to the overlay, `abs_x`/`abs_y` are world coordinates.** While an entity has an `overlay` (it is held, riding, stuck), `x` and `y` are offsets from that overlay. Pick the right pair before doing world-space math.

```lua
local ent = get_entity(players[1].holding_uid)
if ent then
    local wx = ent.overlay and ent.abs_x or ent.x -- world position either way
    local wy = ent.overlay and ent.abs_y or ent.y
    print(wx, wy)
end
```

**Re-attach after changing the hitbox offset.** The hitbox centre is the position plus `offsetx`, so to change the offset without a visible jump also shift the position by the old minus the new offset. An overlaid entity must be freed with `move_entity` first and attached again afterwards.

```lua
local ent, new_offsetx = players[1], 0.3
local overlay_uid = ent.overlay and ent.overlay.uid
local x, y = get_position(ent.uid)
if overlay_uid then
    move_entity(ent.uid, x, y, ent.velocityx, ent.velocityy)
end
ent:set_position(x + (ent.offsetx - new_offsetx), y)
ent.offsetx = new_offsetx
if overlay_uid then
    attach_entity(overlay_uid, ent.uid)
end
```

**`angle` is in radians and does not wrap.** It rotates the sprite, so copy it to a spawned projectile to rotate that the same way. Wrap it into (-pi, pi] yourself when you accumulate it, and set it to 0 to stand upright. The direction of a sprite that points left at angle 0 is `(-cos, -sin)`.

```lua
local turret = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_LASER_TRAP)[1])
if turret then
    local dx, dy = -math.cos(turret.angle), -math.sin(turret.angle)
    local shot = get_entity(spawn(ENT_TYPE.ITEM_LASERTRAP_SHOT, turret.x + 0.5 * dx, turret.y + 0.5 * dy,
        turret.layer, 0.3 * dx, 0.3 * dy))
    shot.angle = turret.angle
end
```

**Tag custom entities with a table in `user_data`.** `user_data` is nil on ordinary entities, so check `type(x.user_data) == "table"` before reading a field. Initialize it in `set_post_entity_spawn`; projectiles resolve to their shooter through `last_owner_uid`.

```lua
set_post_entity_spawn(function(ent)
    ent:set_pre_damage(function(_, dealer)
        local owner = dealer and get_entity(dealer.last_owner_uid)
        if owner and type(owner.user_data) == "table" and owner.user_data.kind == "boss" then
            return false -- ignore this damage
        end
    end)
end, SPAWN_TYPE.ANY, MASK.PLAYER)
```

**Compare `get_texture()` with `type.texture` to find a custom skin.** `EntityDB.texture` is the default texture of the type, so `ent:get_texture() ~= ent.type.texture` tells whether a replacement is still active, and `set_texture(ent.type.texture)` reverts it.

```lua
local ent = players[1]
if ent:get_texture() ~= ent.type.texture then
    ent:set_texture(ent.type.texture) -- back to the vanilla sheet
end
```

**Remap `animation_frame` after the game wrote it.** The game keeps writing vanilla frame numbers, so after `set_texture` with a re-laid-out sheet translate them in a post-update callback (after the vanilla update, not before). Skip frozen entities.

```lua
local remap = { [0] = 16, [1] = 17, [2] = 18 } -- vanilla frame -> custom frame
local ent = get_entity(get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1])
if ent then
    ent:set_post_update_state_machine(function(self)
        if self.frozen_timer > 0 then return end
        self.animation_frame = remap[self.animation_frame] or self.animation_frame
    end)
end
```
