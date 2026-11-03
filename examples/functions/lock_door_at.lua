-- Lock the door at the first player's position so it can not be entered.
local x, y = get_position(players[1].uid)
lock_door_at(x, y)
