-- Turn the first player around by toggling the facing-left flag.
local uid = players[1].uid
set_entity_flags(uid, flip_flag(get_entity_flags(uid), ENT_FLAG.FACING_LEFT))
