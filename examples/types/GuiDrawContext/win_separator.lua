-- Draw a horizontal line between two items of the script options window.
register_option_callback("", {}, function(ctx)
    ctx:win_text("First") ctx:win_separator() ctx:win_text("Second")
end)
