-- Run code after a door is activated by the player.
local door = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_DOOR_EXIT)[1])
door:set_post_activate(function(_, activator) print("activated by", activator and activator.uid) end)
