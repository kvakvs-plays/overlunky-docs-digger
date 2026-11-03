-- Print the input buttons of the rider of a mount, treating it as a Player.
local mount = get_entity(get_entities_by_type(ENT_TYPE.MOUNT_TURKEY)[1])
print(get_entity(mount.rider_uid):as_player().input.buttons_gameplay)
