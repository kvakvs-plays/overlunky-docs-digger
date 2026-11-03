-- Open a movable window that shows the current level every frame.
set_callback(function(ctx)
    ctx:window("Debug", 0, 0, 0, 0, true, function(c) c:win_text("Level: " .. state.level) end)
end, ON.GUIFRAME)
