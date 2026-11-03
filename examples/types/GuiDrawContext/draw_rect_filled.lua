-- Highlight the hitbox of the first player with a translucent fill.
local box = get_hitbox(players[1].uid)
set_callback(function(ctx) ctx:draw_rect_filled(screen_aabb(box), 0, rgba(255, 0, 0, 100)) end, ON.GUIFRAME)
