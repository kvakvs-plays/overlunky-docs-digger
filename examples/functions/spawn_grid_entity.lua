-- Spawn a floor block just above the first player.
local x, y, layer = get_position(players[1].uid)
spawn_grid_entity(ENT_TYPE.FLOORSTYLED_GUTS, x, y + 2, layer)
