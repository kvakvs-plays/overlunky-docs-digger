-- Add a text input field to the script options window.
local name = ""
register_option_callback("name", name, function(ctx) name = ctx:win_input_text("Name", name) return name end)
