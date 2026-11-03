-- Remove the floor tile below the first player.
local x, y, layer = get_position(players[1].uid)
destroy_grid(x, y - 1, layer)
