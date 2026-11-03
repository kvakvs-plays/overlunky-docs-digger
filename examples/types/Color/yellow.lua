-- Draw yellow text on screen every frame.
set_callback(function(ctx)
    ctx:draw_text("Hello", 0, 0, 0.001, 0.001, Color.yellow(), VANILLA_TEXT_ALIGNMENT.CENTER, VANILLA_FONT_STYLE.NORMAL)
end, ON.RENDER_POST_HUD)
