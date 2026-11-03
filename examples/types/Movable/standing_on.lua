-- Print the type of the entity the player is standing on.
local floor = players[1]:standing_on()
if floor then print(floor.type.id) end
