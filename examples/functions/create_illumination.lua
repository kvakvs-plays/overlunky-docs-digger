-- Add a warm light of radius 20 at the first player's position.
local x, y = get_position(players[1].uid)
local light = create_illumination(Color:new(0.9, 0.6, 0, 1), 20, x, y)
print(light)
