-- Spawn a companion next to the first player.
local x, y, layer = get_position(players[1].uid)
spawn_companion(ENT_TYPE.CHAR_GUY_SPELUNKY, x + 1, y, layer)
