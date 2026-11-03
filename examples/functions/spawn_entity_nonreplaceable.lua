-- Spawn a rock that no other script can replace with something else.
local x, y, layer = get_position(players[1].uid)
spawn_entity_nonreplaceable(ENT_TYPE.ITEM_ROCK, x, y + 1, layer, 0, 0)
