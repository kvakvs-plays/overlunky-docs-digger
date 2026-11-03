-- Check whether the first player wears spike shoes.
local has_spike_shoes = players[1]:has_powerup(ENT_TYPE.ITEM_POWERUP_SPIKE_SHOES)
print(has_spike_shoes)
