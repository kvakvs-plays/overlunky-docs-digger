-- Spawn a rock next to the player and make them pick it up.
local x, y, layer = get_position(players[1].uid)
players[1]:pick_up(get_entity(spawn(ENT_TYPE.ITEM_ROCK, x, y, layer, 0, 0)))
