-- Draw a rectangle behind the game UI by switching to the background draw layer first.
set_callback(function(ctx)
    ctx:draw_layer(DRAW_LAYER.BACKGROUND)
    ctx:draw_rect_filled(-0.5, 0.5, 0.5, -0.5, 5, rgba(0, 0, 255, 128))
end, ON.GUIFRAME)
