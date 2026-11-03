-- Shrink the first player's hitbox a bit.
local hitbox = get_hitbox(players[1].uid)
hitbox:extrude(-0.1)
