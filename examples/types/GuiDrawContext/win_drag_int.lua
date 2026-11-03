-- Add a draggable integer field limited to 1..4 to the script options window.
local lvl = 1
register_option_callback("lvl", lvl, function(ctx) lvl = ctx:win_drag_int("Level", lvl, 1, 4) return lvl end)
