**Cling to surfaces with `standing_on_uid`.** When it is not -1 the entity touches something: remember it, reset the field, turn gravity off and wall collision off, and move by writing `velocityx`/`velocityy`. Otherwise restore both flags.

```lua
local x, y, layer = get_position(players[1].uid)
local crawler = get_entity(spawn(ENT_TYPE.MONS_SNAKE, x + 2, y, layer, 0, 0))
crawler:set_post_update_state_machine(function(self)
    if self.standing_on_uid ~= -1 then
        self.user_data = { surface_uid = self.standing_on_uid } -- remember what it clings to
        self.standing_on_uid = -1
        self.flags = clr_flag(set_flag(self.flags, ENT_FLAG.NO_GRAVITY), ENT_FLAG.COLLIDES_WALLS)
    else
        self.flags = set_flag(clr_flag(self.flags, ENT_FLAG.NO_GRAVITY), ENT_FLAG.COLLIDES_WALLS)
    end
end)
```

**Repeat `set_behavior` every frame from the post-update callback.** It overrides the behavior the AI chose during the update, so calling it once is not enough. Reset `state`, `last_state` and `move_state` too and set a short `lock_input_timer` to park a monster. Some projectiles ignore applied `velocityx` for 40 frames after spawn; setting `stand_counter = 40` skips that.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_SCORPION)[1])
if monster then
    monster:set_post_update_state_machine(function(self)
        self:set_behavior(9) -- falling, the same id as the state below
        self.state, self.last_state, self.move_state = 9, 9, 0
        self.lock_input_timer = 5
        self.velocityx = 0.05 -- then drive it yourself
    end)
end
```

**Knockback through `damage`, direct velocity on dead targets.** The `velocity_x`/`velocity_y` arguments of `damage` do nothing for a target with `health` 0, so set `velocityx`/`velocityy` yourself.

```lua
local source, victim = players[1], get_entity(get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1])
if victim then
    local dir = get_position(victim.uid) > get_position(source.uid) and 1 or -1
    if victim.health > 0 then
        victim:damage(source.uid, 1, 0, dir * 0.1, 0.1, 60)
    else
        victim.velocityx, victim.velocityy = dir * 0.1, 0.1
    end
end
```

**Set `last_owner_uid` for attribution.** Spawned projectiles and thrown monsters keep the uid of whoever launched them there, and damage hooks can resolve the real attacker from it.

```lua
local x, y, layer = get_position(players[1].uid)
local arrow = get_entity(spawn(ENT_TYPE.ITEM_WOODEN_ARROW, x, y, layer, 0.2, 0))
arrow.last_owner_uid = players[1].uid
```

**Swap a held item by spawning, picking up, destroying.** Check that the overlay is a player before treating it as a holder. A worn back item must be taken off with `unequip_backitem` before `pick_up`; `overlay:drop(self)` makes a holder let go.

```lua
local item = get_entity(players[1].holding_uid)
local holder = item and item.overlay
if holder and holder.type.search_flags == MASK.PLAYER then
    local replacement = get_entity(spawn(ENT_TYPE.ITEM_ROCK, item.abs_x, item.abs_y, item.layer, 0, 0))
    holder:pick_up(replacement)
    item:destroy()
end
```
