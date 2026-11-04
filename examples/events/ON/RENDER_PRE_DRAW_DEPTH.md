**The callback runs once per draw depth, so filter first.** It is called for every depth in every frame, and returning `true` skips drawing that whole depth. Compare `draw_depth` before drawing and return nothing otherwise, or your sprite is drawn many times per frame.

```lua
local DEPTH <const> = 40 -- stand-in: the depth layer you want to draw into
set_callback(function(ctx, draw_depth)
    if draw_depth ~= DEPTH then return end
    ctx:draw_world_texture(TEXTURE.DATA_TEXTURES_HUD_0, 3, 7, 10, 100, 11, 99, Color.white())
end, ON.RENDER_PRE_DRAW_DEPTH)
```

**Advance animations only inside the depth check.** State changed outside the filter moves once per depth instead of once per frame. Also skip the update while `state.pause` is set so the animation freezes with the game.

```lua
local DEPTH <const> = 40
local x = 0
set_callback(function(ctx, draw_depth)
    if draw_depth ~= DEPTH then return end
    if state.pause == 0 then x = x + 0.05 end
    ctx:draw_world_texture(TEXTURE.DATA_TEXTURES_HUD_0, 3, 7, x, 100, x + 1, 99, Color.white())
end, ON.RENDER_PRE_DRAW_DEPTH)
```
