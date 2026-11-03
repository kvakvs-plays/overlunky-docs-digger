-- Get where the first player is drawn, which can differ from the physics position.
local x, y, layer = get_render_position(players[1].uid)
print(x, y, layer)
