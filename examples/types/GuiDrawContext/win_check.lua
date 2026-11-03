-- Add a checkbox to the script options window; the returned value is saved in the script options.
local on = false
register_option_callback("flag", on, function(ctx) on = ctx:win_check("Enable feature", on) return on end)
