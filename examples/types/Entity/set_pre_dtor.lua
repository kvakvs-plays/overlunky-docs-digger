-- Clean up a per-entity GUI callback when the entity object is finally deleted.
local ent, cb = players[1], set_callback(function(_) end, ON.GUIFRAME)
ent:set_pre_dtor(function() clear_callback(cb) end)
