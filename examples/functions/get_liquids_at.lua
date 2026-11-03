-- Count the water and lava tiles at the first player's position.
local x, y, layer = get_position(players[1].uid)
print(get_liquids_at(x, y, layer))
