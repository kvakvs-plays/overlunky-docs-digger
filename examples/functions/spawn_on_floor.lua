-- Spawn a mantrap on the floor next to the first player.
local x, y, layer = get_position(players[1].uid)
spawn_on_floor(ENT_TYPE.MONS_MANTRAP, x + 2, y, layer)
