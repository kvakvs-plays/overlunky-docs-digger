-- Check if a specific item is held by or attached to the first player.
local rock = get_entities_by_type(ENT_TYPE.ITEM_ROCK)[1]
local is_held = rock and entity_has_item_uid(players[1].uid, rock)
print(is_held)
