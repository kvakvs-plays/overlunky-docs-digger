-- Keep only the solid entities of a list.
local floors = get_entities_by(0, MASK.FLOOR, LAYER.FRONT)
local solids = filter_entities(floors, function(uid) return test_flag(get_entity_flags(uid), ENT_FLAG.SOLID) end)
print(#solids)
