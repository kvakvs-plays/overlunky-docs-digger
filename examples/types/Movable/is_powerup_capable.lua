-- Check that an entity can carry powerups before asking for its back item.
local ent = players[1]
if ent:is_powerup_capable() then print(ent:worn_backitem()) end
