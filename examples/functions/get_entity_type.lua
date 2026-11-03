-- Check what kind of floor is below the first player.
local x, y, layer = get_position(players[1].uid)
local is_generic = get_entity_type(get_grid_entity_at(x, y - 1, layer)) == ENT_TYPE.FLOOR_GENERIC
print(is_generic)
