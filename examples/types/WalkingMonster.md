**Force an aggro.** Observed for walking monsters such as tikiman and caveman: `move_state == 6` means aggroed and running at the target, while 0, 1, 2 and 5 are calm. Writing 6 together with `chased_target_uid` starts a chase on your own condition, for example by distance and ignoring walls.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_TIKIMAN)[1])
monster:set_post_update_state_machine(function(self)
    local calm = self.move_state == 0 or self.move_state == 1 or self.move_state == 2
    if calm and distance(self.uid, players[1].uid) < 5 then
        self.chased_target_uid = players[1].uid
        self.move_state = 6
    end
end)
```

**Keep a chase going after a wall bump in the air.** Observed: `move_state` 4 is set when the monster hits a wall while airborne, and it blocks turning until landing. Remapping it to 6 keeps the chase alive.

```lua
local monster = get_entity(get_entities_by_type(ENT_TYPE.MONS_TIKIMAN)[1])
monster:set_post_update_state_machine(function(self)
    if self.move_state == 4 then self.move_state = 6 end
end)
```

**Block idle chatting.** Setting `chatting_to_uid` of a tikiman to a uid that no entity has (observed to work with 99999) stops the idle chat behavior.

```lua
local tikiman = get_entity(get_entities_by_type(ENT_TYPE.MONS_TIKIMAN)[1])
tikiman:set_post_update_state_machine(function(self)
    self.chatting_to_uid = 99999
end)
```

**Recognize the chat length.** Observed: when a vanilla chat begins, `walk_pause_timer` is set to 180, 120 or 60, matching the length of the chat animation and voice line. Read it on the first frame of the pause, for example to pick a matching replacement sound.

```lua
local tikiman = get_entity(get_entities_by_type(ENT_TYPE.MONS_TIKIMAN)[1])
local last_pause = 0
tikiman:set_post_update_state_machine(function(self)
    local t = self.walk_pause_timer
    if t > last_pause and (t == 180 or t == 120 or t == 60) then
        print("chat of " .. t .. " frames started")
    end
    last_pause = t
end)
```
