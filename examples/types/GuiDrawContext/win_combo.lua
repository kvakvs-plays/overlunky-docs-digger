-- Add a dropdown to the script options window: options are separated by "\0" and the list ends with "\0\0".
local mode = 1
register_option_callback("mode", mode, function(ctx)
    mode = ctx:win_combo("Mode", mode, "Off\0Normal\0Hard\0\0") return mode
end)
