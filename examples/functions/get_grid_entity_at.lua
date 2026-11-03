-- Get the uid of the floor tile below the first player, or -1 when the cell is empty.
local x, y, layer = get_position(players[1].uid)
local floor_uid = get_grid_entity_at(x, y - 1, layer)
print(floor_uid)
