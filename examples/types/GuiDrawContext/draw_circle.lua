-- Outline a 1-tile radius around the first player.
local x, y = get_position(players[1].uid)
set_callback(function(ctx)
    local sx, sy = screen_position(x, y)
    ctx:draw_circle(sx, sy, screen_distance(1), 1, rgba(255, 0, 0, 255))
end, ON.GUIFRAME)
