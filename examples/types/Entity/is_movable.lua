-- Walk up the overlay chain of an entity and handle only the movable ones.
local current = players[1]
while current do
    if current:is_movable() then print(current.uid) end
    current = current.overlay
end
