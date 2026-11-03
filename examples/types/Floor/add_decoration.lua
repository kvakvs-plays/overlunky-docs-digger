-- Add a decoration to the top side of a floor tile.
local floor = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1])
floor:add_decoration(FLOOR_SIDE.TOP)
