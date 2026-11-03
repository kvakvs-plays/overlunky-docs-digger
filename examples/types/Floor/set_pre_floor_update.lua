-- Run code before a floor tile updates, e.g. to store its state.
local floor = get_entity(get_entities_by_type(ENT_TYPE.FLOOR_GENERIC)[1])
floor:set_pre_floor_update(function(self) toast("Updating floor " .. self.uid) end)
