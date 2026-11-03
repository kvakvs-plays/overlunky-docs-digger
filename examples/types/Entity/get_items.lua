-- Kill all entities attached to a floor tile.
for _, uid in ipairs(get_entity(get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1]):get_items()) do kill_entity(uid) end
