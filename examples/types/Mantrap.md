**Let an entity survive being eaten.** `eaten_uid` is the uid of the entity the mantrap holds; the game uses it to burst the entity out after the stun, or to gib it if the mantrap dies. To make a non-NPC entity go through this, do it from its `set_pre_kill` when a mantrap is the cause, and return true to cancel the kill. Observed: `destroy_corpse` is true in this case.

```lua
local prey = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
prey:set_pre_kill(function(self, destroy_corpse, by)
    if not (destroy_corpse and by and by.type.id == ENT_TYPE.MONS_MANTRAP) then return end
    if test_flag(self.flags, ENT_FLAG.DEAD) then return end
    attach_entity(by.uid, self.uid)
    by.eaten_uid = self.uid
    self.velocityx, self.velocityy = 0, 0
    self.flags = set_flag(self.flags, ENT_FLAG.INVISIBLE)
    self.flags = set_flag(self.flags, ENT_FLAG.PASSES_THROUGH_OBJECTS)
    self.flags = set_flag(self.flags, ENT_FLAG.PAUSE_AI_AND_PHYSICS)
    return true
end)
```

**Clear the pause flag again.** Vanilla clears `PAUSE_AI_AND_PHYSICS` only for NPCs, so for other prey it must be cleared when the mantrap is killed or destroyed (the prey bursts out or is gibbed).

```lua
local mantrap = get_entity(get_entities_by_type(ENT_TYPE.MONS_MANTRAP)[1])
local function release_prey(prey_uid)
    local prey = get_entity(prey_uid)
    if prey then prey.flags = clr_flag(prey.flags, ENT_FLAG.PAUSE_AI_AND_PHYSICS) end
end
mantrap:set_pre_kill(function(self) release_prey(self.eaten_uid) end)
mantrap:set_pre_destroy(function(self) release_prey(self.eaten_uid) end)
```

**Stop a mantrap from eating something.** Returning true from `set_pre_on_collision2` suppresses the vanilla reaction to the overlap, so the mantrap ignores the other entity.

```lua
local mantrap = get_entity(get_entities_by_type(ENT_TYPE.MONS_MANTRAP)[1])
mantrap:set_pre_on_collision2(function(_, other)
    return other.type.id == ENT_TYPE.MONS_GIANTFISH
end)
```
