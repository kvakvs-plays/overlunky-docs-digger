-- Check if the first player has an ankh powerup.
local has_ankh = entity_has_item_type(players[1].uid, ENT_TYPE.ITEM_POWERUP_ANKH)
print(has_ankh)
