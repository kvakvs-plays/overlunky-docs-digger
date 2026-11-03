-- Count the webs overlapping the first player's hitbox.
local ent = players[1]
print(#get_entities_overlapping_hitbox(ENT_TYPE.ITEM_WEB, MASK.ITEM, ent:get_hitbox(), ent.layer))
