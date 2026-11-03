-- Spin an entity a little every frame, after the game has updated it.
local ent = get_entity(get_entities_by_type(ENT_TYPE.ITEM_ROCK)[1])
ent:set_post_update_state_machine(function(self) self.angle = self.angle + 0.05 end)
