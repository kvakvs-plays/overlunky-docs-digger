-- Check whether the player's position lies inside their own hitbox with 0.4 of extra margin.
local x, y = get_position(players[1].uid)
print(get_hitbox(players[1].uid, 0.4):is_point_inside(x, y))
