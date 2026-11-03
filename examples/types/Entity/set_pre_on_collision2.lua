-- Ignore collisions between an entity and a specific type of other entity.
local ent = get_entity(get_entities_by_type(ENT_TYPE.ITEM_ROCK)[1])
ent:set_pre_on_collision2(function(_, other) return other.type.id == ENT_TYPE.MONS_SHOPKEEPER end)
