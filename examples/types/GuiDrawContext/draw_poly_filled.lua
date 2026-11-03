-- Fill a triangle on the screen; the points are Vec2 in screen coordinates.
local pts = { Vec2.new(0, 0), Vec2.new(0.2, 0), Vec2.new(0.1, 0.2) }
set_callback(function(ctx) ctx:draw_poly_filled(pts, rgba(0, 128, 255, 128)) end, ON.GUIFRAME)
