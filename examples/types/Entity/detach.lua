-- Make an entity let go of whatever it is attached to, e.g. before it jumps away.
local ent = get_entity(get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1])
ent:detach(false)
