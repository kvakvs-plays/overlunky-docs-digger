-- Spawn a gold nugget where a floor tile was destroyed.
local floor = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1])
local x, y, layer = get_position(floor.uid)
floor:set_post_destroy(function(_) spawn(ENT_TYPE.ITEM_NUGGET, x, y, layer, 0, 0) end)
