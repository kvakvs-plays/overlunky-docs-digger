-- Kill a snake, if the level has one.
local uid = get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1]
if uid then kill_entity(uid) end
