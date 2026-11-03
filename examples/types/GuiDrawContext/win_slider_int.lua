-- Add an integer slider from 0 to 4 to the script options window.
local stage = 0
register_option_callback("stage", stage, function(ctx)
    stage = ctx:win_slider_int("Stage", stage, 0, 4) return stage
end)
