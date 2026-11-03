-- React when a monster collides with another movable entity.
local ent = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
ent:set_post_on_collision2(function(_, other) print("hit", other.uid) end)
