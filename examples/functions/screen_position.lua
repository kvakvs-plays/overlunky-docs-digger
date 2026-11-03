-- Convert the first player's world position to screen coordinates.
local x, y = get_position(players[1].uid)
print(screen_position(x, y))
