-- Make the next widget half as wide as the window.
local mode = 1
register_option_callback("mode", mode, function(ctx)
    ctx:win_width(0.5) mode = ctx:win_combo("Mode", mode, "Off\0On\0\0") return mode
end)
