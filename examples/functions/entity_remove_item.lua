-- Detach an item from the entity it is attached to, without destroying it.
local rock = get_entities_by_type(ENT_TYPE.ITEM_ROCK)[1]
local ent = rock and get_entity(rock)
if ent and ent.overlay then entity_remove_item(ent.overlay.uid, rock) end
