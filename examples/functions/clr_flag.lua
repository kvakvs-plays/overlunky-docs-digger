-- Make the first player unable to be picked up by clearing a flag of their entity.
local uid = players[1].uid
set_entity_flags(uid, clr_flag(get_entity_flags(uid), ENT_FLAG.PICKUPABLE))
