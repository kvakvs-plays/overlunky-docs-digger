-- Spawn a landmine resting on the floor below the first player.
local x, y, layer = get_position(players[1].uid)
spawn_entity_snapped_to_floor(ENT_TYPE.ITEM_LANDMINE, x, y, layer)
