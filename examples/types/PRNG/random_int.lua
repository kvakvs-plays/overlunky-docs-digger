-- Spawn blood at the player with a random horizontal offset of up to 0.1 tiles.
local x, y, layer = get_position(players[1].uid)
spawn(ENT_TYPE.ITEM_BLOOD, x + prng:random_int(-10, 10, PRNG_CLASS.PARTICLES) / 100, y, layer, 0, 0.1)
