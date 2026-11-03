-- Spawn a crate at the first player that contains a bomb bag.
local x, y, layer = get_position(players[1].uid)
set_contents(spawn_grid_entity(ENT_TYPE.ITEM_CRATE, x, y, layer), ENT_TYPE.ITEM_PICKUP_BOMBBAG)
