-- Draw a line between two world positions.
local x1, y1 = screen_position(10, 100)
local x2, y2 = screen_position(20, 100)
set_callback(function(ctx) ctx:draw_line(x1, y1, x2, y2, 1, rgba(255, 0, 0, 255)) end, ON.GUIFRAME)
