-- Draw a texture on screen without tinting it.
set_callback(function(ctx)
    ctx:draw_screen_texture(TEXTURE.DATA_TEXTURES_HUD_0, 0, 0, -0.1, 0.1, 0.1, -0.1, Color.white())
end, ON.RENDER_POST_HUD)
