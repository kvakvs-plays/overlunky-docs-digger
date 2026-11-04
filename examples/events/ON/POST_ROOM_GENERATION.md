**It runs for every generated level, so guard first.** The context edits room templates before entities are spawned. Check the screen or theme before touching anything, otherwise the change hits every level.

```lua
set_callback(function(ctx)
    if state.theme ~= THEME.DWELLING then return end
    ctx:set_room_template(0, 0, LAYER.FRONT, ROOM_TEMPLATE.SOLID)
end, ON.POST_ROOM_GENERATION)
```
