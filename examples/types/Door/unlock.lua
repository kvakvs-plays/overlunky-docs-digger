-- Open the first exit door again after it was locked: brings back its button prompt and the open frame of its BG_DOOR.
local door = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_DOOR_EXIT)[1])
door:unlock(true)
