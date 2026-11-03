-- Check whether the first player touches an area or another entity.
local ent = get_entity(get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1])
print(players[1]:overlaps_with(ent), players[1]:overlaps_with(AABB:new(0, 10, 10, 0)))
