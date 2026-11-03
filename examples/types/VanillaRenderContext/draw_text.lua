-- Write a centered bold text in the middle of the screen.
set_callback(function(ctx)
    ctx:draw_text("Hello", 0, 0, 0.002, 0.002, Color.white(), VANILLA_TEXT_ALIGNMENT.CENTER, VANILLA_FONT_STYLE.BOLD)
end, ON.RENDER_POST_HUD)
