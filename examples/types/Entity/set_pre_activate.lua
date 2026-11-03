-- React when a player presses the door button on a pet, before the game handles it.
local pet = get_entity(get_entities_by_type(ENT_TYPE.MONS_PET_DOG)[1])
pet:set_pre_activate(function(self, activator) toast(activator.uid .. " activated " .. self.uid) end)
