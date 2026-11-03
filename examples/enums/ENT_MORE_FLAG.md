**Impact flags are sticky.** The game sets `HIT_GROUND` and `HIT_WALL` on collision but nothing clears them. To detect a new impact, test the flag in a post-update hook and clear it yourself.

```lua
local ent = players[1]
ent:set_post_update_state_machine(function(self)
    if test_flag(self.more_flags, ENT_MORE_FLAG.HIT_GROUND) and math.abs(self.velocityy) > 0.03 then
        print("landed")
        self.more_flags = clr_flag(self.more_flags, ENT_MORE_FLAG.HIT_GROUND)
    end
end)
```

**Holding a player in place.** `DISABLE_INPUT` stops the player's controls. Zero the velocity and stun, and drop gravity and collisions so nothing pushes them while parked. Clear the flag to give control back.

```lua
local p = players[1]
p.velocityx, p.velocityy, p.stun_timer = 0, 0, 0
p.more_flags = set_flag(p.more_flags, ENT_MORE_FLAG.DISABLE_INPUT)
p.flags = set_flag(set_flag(p.flags, ENT_FLAG.NO_GRAVITY), ENT_FLAG.PASSES_THROUGH_OBJECTS)
-- later: p.more_flags = clr_flag(p.more_flags, ENT_MORE_FLAG.DISABLE_INPUT)
```

**Hiding a held player completely.** `INVISIBLE` on the player does not hide what they carry. Hide the held entity and the worn back item too.

```lua
local p = players[1]
p.flags = set_flag(p.flags, ENT_FLAG.INVISIBLE)
local held = get_entity(p.holding_uid)
if held then held.flags = set_flag(held.flags, ENT_FLAG.INVISIBLE) end
local back = get_entity(p:worn_backitem())
if back then back.color.a = 0.0 end
```

**Liquid check without corpses.** `SWIMMING` tells whether the entity is currently in liquid. Combine it with a `health` test when dead bodies should be ignored.

```lua
for _, uid in ipairs(get_entities_by_type(ENT_TYPE.MONS_SNAKE)) do
    local m = get_entity(uid)
    if m and m.health ~= 0 and test_flag(m.more_flags, ENT_MORE_FLAG.SWIMMING) then
        m:kill(false, nil)
    end
end
```
