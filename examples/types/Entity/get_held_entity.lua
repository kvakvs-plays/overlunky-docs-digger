-- Find out what the first player is holding.
local held = players[1]:get_held_entity()
if held then print(held.uid, held.type.id) end
