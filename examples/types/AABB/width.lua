-- Pick a random x position inside the player's hitbox, e.g. for spawning something within it.
local box = players[1]:get_hitbox()
print(box.left + math.random() * box:width())
