-- Silently take a floor tile out of the level without breaking it, so it can be replaced.
get_entity(get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1]):remove()
