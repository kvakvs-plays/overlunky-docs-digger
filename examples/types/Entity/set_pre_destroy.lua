-- Run code just before an entity is removed from the level, e.g. to clean up related state.
local ent = get_entity(get_entities_by_type(ENT_TYPE.MONS_UFO)[1])
ent:set_pre_destroy(function(self) toast("Removing " .. self.uid) end)
