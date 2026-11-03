-- Spawn another copy of player slot 1 next to the first player.
local x, y = get_position(players[1].uid)
spawn_player(1, x + 1, y)
