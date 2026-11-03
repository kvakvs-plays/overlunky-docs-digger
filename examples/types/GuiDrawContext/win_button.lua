-- Add a button to the script options window that heals the first player.
register_option_callback("reset", false, function(ctx)
    if ctx:win_button("Heal") then players[1].health = 4 end
end)
