-- Stop a per-entity callback that was set earlier.
local uid = players[1].uid
clear_entity_callback(uid, set_pre_statemachine(uid, function(_) end))
