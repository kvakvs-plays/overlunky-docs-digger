-- Check whether an entity is moving down, including the velocity of what it stands on.
local ent = players[1]
print(ent:get_absolute_velocity().y < 0)
