-- Show the current level theme as debug text on the screen.
set_callback(function(ctx)
    ctx:draw_text(-0.9, 0.9, 0, "THEME: " .. state.theme, rgba(255, 255, 255, 255))
end, ON.GUIFRAME)
