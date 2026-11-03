-- Light up the first torch of the level.
local torch = get_entity(get_entities_by_type(ENT_TYPE.ITEM_TORCH)[1])
torch:light_up(true)
