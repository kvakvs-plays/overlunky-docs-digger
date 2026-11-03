-- Outline the hitbox of the first player.
local box = get_hitbox(players[1].uid)
set_callback(function(ctx) ctx:draw_rect(screen_aabb(box), 2, 0, rgba(0, 255, 0, 255)) end, ON.GUIFRAME)
