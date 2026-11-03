**Make a caveman pick something up.** To have a caveman pick up an entity it normally ignores, call `pick_up` from a collision callback only when it is standing (`state == 1`), both `cooldown_timer` and `can_pick_up_timer` are zero and the target stands on something. Afterwards pause the AI briefly so it does not drop the item at once.

```lua
local caveman = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
caveman:set_post_on_collision2(function(e, o)
    if e.state == 1 and e.cooldown_timer == 0 and e.can_pick_up_timer == 0
        and o.standing_on_uid ~= -1 and o.type.id == ENT_TYPE.MONS_CRITTERCRAB then
        e:pick_up(o)
        e.walk_pause_timer = 30
        e.move_state = 0
    end
    return false
end)
```
