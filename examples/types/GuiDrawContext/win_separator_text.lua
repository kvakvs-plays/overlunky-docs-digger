-- Start a labelled group in the script options window.
register_option_callback("", {}, function(ctx)
    ctx:win_separator_text("Gameplay")
end)
