**Take over the swimming.** A vanilla tadpole falls back to its own AI each frame. Setting `lock_input_timer` high every frame keeps it off while your script sets the velocity; setting it back to 0 hands control to vanilla. `chased_target_uid` is plain data you can use for your own target (`-1` for none).

```lua
local tadpole = get_entity(get_entities_by_type(ENT_TYPE.MONS_TADPOLE)[1])
tadpole:set_post_update_state_machine(function(self)
    self.lock_input_timer = 512
    local target = get_entity(self.chased_target_uid)
    if target == nil then
        self.chased_target_uid = -1
        return
    end
    self.velocityx = (target.x - self.x) * 0.05
end)
```

**Check that the target is swimming.** Observed: a player whose `wet_effect_timer` is 300 is currently in the water, which makes a handy condition for dropping the chase.

```lua
local tadpole = get_entity(get_entities_by_type(ENT_TYPE.MONS_TADPOLE)[1])
tadpole:set_post_update_state_machine(function(self)
    local target = get_entity(self.chased_target_uid)
    if target and target.wet_effect_timer ~= 300 then
        self.chased_target_uid = -1
        self.lock_input_timer = 0
    end
end)
```
