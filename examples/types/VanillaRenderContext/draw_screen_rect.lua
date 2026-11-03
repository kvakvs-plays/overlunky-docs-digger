-- Outline a rectangle in the middle of the screen with a red line.
set_callback(function(ctx)
    ctx:draw_screen_rect(AABB.new(-0.5, 0.5, 0.5, -0.5), 5.0, Color.red())
end, ON.RENDER_POST_HUD)
