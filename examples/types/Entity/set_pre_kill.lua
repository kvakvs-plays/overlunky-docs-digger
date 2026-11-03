-- Stop a monster from dying while it is frozen: returning true skips the original kill.
local ent = get_entity(get_entities_by_type(ENT_TYPE.MONS_MANTRAP)[1])
ent:set_pre_kill(function(self, _, _) return self.frozen_timer > 0 end)
