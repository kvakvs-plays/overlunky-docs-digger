-- Find all players within a radius of 2 tiles around the first player.
local x, y, layer = get_position(players[1].uid)
local nearby = get_entities_at(0, MASK.PLAYER, x, y, layer, 2)
print(#nearby)
