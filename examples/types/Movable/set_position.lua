-- Move the first player two tiles up without touching their velocity.
local x, y = get_position(players[1].uid)
players[1]:set_position(x, y + 2)
