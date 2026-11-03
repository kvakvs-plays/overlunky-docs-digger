-- Build a box 2 tiles wide and 0.5 tall around the player, then grow it by 0.25 in every direction.
local box = players[1]:get_hitbox():extrude(1, 0.25)
box:extrude(0.25)
