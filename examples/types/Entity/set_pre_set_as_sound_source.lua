-- Prevent an entity from becoming the sound source of its own sounds.
local ent = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
ent:set_pre_set_as_sound_source(function(_, _) return true end)
