-- Cover the whole screen with a black rectangle.
set_callback(function(ctx)
    ctx:draw_screen_rect_filled(AABB.new(-1, 1, 1, -1), Color.black())
end, ON.RENDER_POST_HUD)
