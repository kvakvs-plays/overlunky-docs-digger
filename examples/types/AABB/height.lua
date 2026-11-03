-- Pick a random height inside the player's hitbox, e.g. for spawning something within it.
local box = players[1]:get_hitbox()
print(box.bottom + math.random() * box:height())
