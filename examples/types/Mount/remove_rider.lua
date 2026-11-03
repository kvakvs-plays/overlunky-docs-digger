-- Throw the rider off the first turkey in the level.
get_entity(get_entities_by_type(ENT_TYPE.MOUNT_TURKEY)[1]):remove_rider()
