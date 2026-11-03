-- Draw the hitbox of the first player on the screen.
local box = get_hitbox(players[1].uid)
set_callback(function(ctx) ctx:draw_rect_filled(screen_aabb(box), 0, rgba(255, 0, 0, 140)) end, ON.GUIFRAME)
