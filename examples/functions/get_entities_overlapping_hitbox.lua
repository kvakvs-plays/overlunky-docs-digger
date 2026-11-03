-- Find the floor tiles inside the first player's hitbox.
local hitbox = get_hitbox(players[1].uid)
local floors = get_entities_overlapping_hitbox(0, MASK.FLOOR, hitbox, players[1].layer)
print(#floors)
