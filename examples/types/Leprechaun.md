**Clinging and ending the cling.** Observed: `hump_timer > 0` while the leprechaun is attached to a victim through its `overlay`, and it reaches 1 right before the cling ends. To end it cleanly from a script, write the state fields below, drop the timer and remove the leprechaun from the victim's items; apply velocity one frame later.

```lua
local leprechaun = get_entity(get_entities_by_type(ENT_TYPE.MONS_LEPRECHAUN)[1])
leprechaun:set_post_update_state_machine(function(self)
    if self.hump_timer ~= 1 then return end
    self.state, self.last_state, self.move_state = 12, 1, 5
    self:set_behavior(12)
    self.timer_after_humping = 120
    self.hump_timer = 0
    if self.overlay then entity_remove_item(self.overlay.uid, self.uid) end
    local uid = self.uid
    set_timeout(function()
        local lep = get_entity(uid)
        if lep then lep.velocityy = 0.21 end
    end, 1)
end)
```

**Drop no treasure.** `gold` is what the leprechaun drops on death, so resetting it every frame keeps a custom variant from dropping loot.

```lua
local leprechaun = get_entity(get_entities_by_type(ENT_TYPE.MONS_LEPRECHAUN)[1])
leprechaun:set_post_update_state_machine(function(self)
    self.gold = 0
end)
```
