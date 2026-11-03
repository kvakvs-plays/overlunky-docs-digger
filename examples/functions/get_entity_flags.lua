-- Check if the floor tile below the first player is solid.
local x, y, layer = get_position(players[1].uid)
local solid = test_flag(get_entity_flags(get_grid_entity_at(x, y - 1, layer)), ENT_FLAG.SOLID)
print(solid)
