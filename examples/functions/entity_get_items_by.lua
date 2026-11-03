-- List the uids of all paste bombs stuck to the first player.
local bombs = entity_get_items_by(players[1].uid, ENT_TYPE.ITEM_PASTEBOMB, MASK.ITEM)
print(#bombs)
