-- Turn the first player to face left.
local uid = players[1].uid
set_entity_flags(uid, set_flag(get_entity_flags(uid), ENT_FLAG.FACING_LEFT))
