-- Find which room (column, row) the first player is in.
local x, y = get_position(players[1].uid)
print(get_room_index(x, y))
