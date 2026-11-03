-- Put two buttons on the same line in the script options window.
register_option_callback("", {}, function(ctx)
    ctx:win_button("Reset") ctx:win_inline() ctx:win_button("Unlock all")
end)
