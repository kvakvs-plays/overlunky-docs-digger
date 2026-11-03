-- Add a draggable float field limited to -8..8 to the script options window.
local dx = 0
register_option_callback("dx", dx, function(ctx) dx = ctx:win_drag_float("Offset X", dx, -8, 8) return dx end)
