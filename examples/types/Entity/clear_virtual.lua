-- Stop an entity hook when it is no longer needed.
local ent = players[1]
local id = ent:set_pre_update_state_machine(function(_) end)
ent:clear_virtual(id)
