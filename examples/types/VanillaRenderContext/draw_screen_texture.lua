-- Draw one sprite (row 0, column 0) of a vanilla HUD texture in the middle of the screen.
set_callback(function(ctx)
    ctx:draw_screen_texture(TEXTURE.DATA_TEXTURES_HUD_2, 0, 0, -0.1, 0.1, 0.1, -0.1, Color.white())
end, ON.RENDER_POST_HUD)
