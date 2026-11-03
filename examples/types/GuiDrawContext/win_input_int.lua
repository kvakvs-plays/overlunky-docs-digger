-- Add an integer input field to the script options window.
local n = 1
register_option_callback("n", n, function(ctx) n = ctx:win_input_int("World", n) return n end)
