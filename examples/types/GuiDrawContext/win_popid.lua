-- Give widgets a unique ID with win_pushid so identical labels do not clash, then pop it again.
register_option_callback("", {}, function(ctx)
    ctx:win_pushid(1) ctx:win_button("Apply") ctx:win_popid()
    ctx:win_pushid(2) ctx:win_button("Apply") ctx:win_popid()
end)
