-- Show a line of text in the script options window.
register_option_callback("", {}, function(ctx)
    ctx:win_text("Current level: " .. state.level)
end)
