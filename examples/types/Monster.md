**There is no "AI off" switch.** To script a vanilla-based monster, overwrite its AI fields every frame from a post-update callback, so you replace what the vanilla AI just decided. Clearing `move_state` and keeping `lock_input_timer` above zero makes it ignore walking and aggro input for the next frames.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_TIKIMAN)[1])
local scripted_vx = 0.05
monster:set_post_update_state_machine(function(self)
    self.move_state = 0
    self.lock_input_timer = 3
    self.velocityx = scripted_vx
end)
```

**Bail out while the monster is frozen, stunned, dead or webbed.** The post-update callback still runs in all of these, so custom movement has to return early. Observed: of the two web-stuck bits in `more_flags`, bit 8 is reset on every state machine tick, so only bit 9 is reliable.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
monster:set_post_update_state_machine(function(self)
    if test_flag(self.more_flags, 9) then return end
    if self.frozen_timer > 0 then return end
    if self.stun_timer ~= 0 or test_flag(self.flags, ENT_FLAG.DEAD) then return end
    self.velocityx = 0.02
end)
```

**Release a carried monster when its stun ends.** Observed: a stunned monster that a player carries is not dropped when `stun_timer` reaches zero. Dropping it through its `overlay` fixes that.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
monster:set_post_update_state_machine(function(self)
    if self.stun_timer == 0 and self.overlay ~= nil and self.overlay.holding_uid == self.uid then
        self.overlay:drop(self)
    end
end)
```

**Stun by a photo camera flash.** There is no damage flag for it. Observed values, not guarantees: the stun that a camera flash applies arrives in `set_pre_stun` with `framecount` 225 (other sources used other numbers), and without a hook the stunned monster shows `stun_timer == 1` on the next frame.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
monster:set_pre_stun(function(self, framecount)
    if framecount ~= 225 then return end
    kill_entity(self.uid)
    return true
end)
```

**Credit a kill to a player.** `responsible` in a kill callback is the direct cause (bomb, arrow, whip), so check both its own uid and its `last_owner_uid`. It can be `nil`, for example for fire.

```lua
local kills = 0
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
monster:set_post_kill(function(_, _, responsible)
    if responsible == nil then return end
    for _, p in ipairs(players) do
        if responsible.uid == p.uid or responsible.last_owner_uid == p.uid then kills = kills + 1 end
    end
end)
```

**Custom death with `set_pre_kill`.** The hook runs before the vanilla gibs, blood, sounds and loot. Calling `destroy()` inside it removes the monster at once, so no vanilla corpse or rubble is left, while the death sound and blood still play. Return early when `destroy_corpse` is true so loot is not dropped a second time for an existing corpse that is being gibbed.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
monster:set_pre_kill(function(self, destroy_corpse, _)
    if destroy_corpse then return end
    local x, y, l = get_position(self.uid)
    spawn(ENT_TYPE.ITEM_EMERALD, x, y, l, 0, 0.1)
    self:destroy()
end)
```

**Damage hooks run before health changes.** In `set_pre_damage` the health is not reduced yet, so a hit is lethal when `health - damage_amount <= 0`. `damage_dealer` is `nil` for environmental damage. Returning true cancels the hit, which also gives one-sided immunity, for example against whips; set `exit_invincibility_timer` so the same whip does not retrigger at once.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
monster:set_pre_damage(function(self, dealer, amount, flags)
    if dealer == nil then return end
    if test_mask(flags, DAMAGE_TYPE.WHIP) and self.exit_invincibility_timer == 0 then
        self.exit_invincibility_timer = 10
        return true
    end
    if self.health - amount <= 0 then
        self.health = 1
        return true
    end
end)
```

**Mute the base type's death voice for one kill.** The death sounds live in the per-type `EntityDB`, so a monster built on a vanilla one plays the vanilla voice. Silence the type inside `set_pre_kill` and restore it a frame later. Another monster of that type dying on the same frame is silent too.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_TIKIMAN)[1])
monster:set_pre_kill(function(_, _, _)
    local db = get_type(ENT_TYPE.MONS_TIKIMAN)
    local by_player, by_other = db.sound_killed_by_player, db.sound_killed_by_other
    db.sound_killed_by_player, db.sound_killed_by_other = -1, -1
    set_timeout(function() db.sound_killed_by_player, db.sound_killed_by_other = by_player, by_other end, 1)
end)
```
