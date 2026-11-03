-- Convert screen coordinates (-1..1) to game positions: the top left and bottom right of the view.
local left, top = game_position(-1, 1)
local right, bottom = game_position(1, -1)
print(left, top, right, bottom)
