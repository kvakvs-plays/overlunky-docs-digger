-- Put the first player on a freshly spawned turkey as its rider.
local x, y, layer = get_position(players[1].uid)
carry(spawn(ENT_TYPE.MOUNT_TURKEY, x, y, layer, 0, 0), players[1].uid)
