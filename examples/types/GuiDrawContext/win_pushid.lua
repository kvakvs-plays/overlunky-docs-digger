-- Give widgets a unique ID so several buttons with the same label work independently.
register_option_callback("", {}, function(ctx)
    for id = 1, 3 do ctx:win_pushid(id) ctx:win_button("Apply") ctx:win_popid() end
end)
