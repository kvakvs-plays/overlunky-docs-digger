-- Make the first player pass through objects.
local player = players[1]
player.flags = set_flag(player.flags, ENT_FLAG.PASSES_THROUGH_OBJECTS)
