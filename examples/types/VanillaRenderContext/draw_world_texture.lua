-- Draw one sprite of a vanilla HUD texture in the level, one tile wide, at world position (10, 100).
set_callback(function(ctx)
    ctx:draw_world_texture(TEXTURE.DATA_TEXTURES_HUD_0, 3, 7, 10, 100, 11, 99, Color.white())
end, ON.RENDER_PRE_DRAW_DEPTH)
