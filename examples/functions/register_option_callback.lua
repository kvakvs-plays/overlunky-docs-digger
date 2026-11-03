-- Add a section to the script options with custom text.
register_option_callback("my_option", false, function(draw_ctx) draw_ctx:win_text("Hello") end)
