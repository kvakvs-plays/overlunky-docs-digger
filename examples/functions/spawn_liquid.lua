-- Pour a bit of water at the first player position.
local x, y = get_position(players[1].uid)
spawn_liquid(ENT_TYPE.LIQUID_WATER, x, y + 1)
