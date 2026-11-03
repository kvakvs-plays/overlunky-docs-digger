-- Make a shopkeeper fire a freshly spawned shotgun.
local shopkeeper = get_entity(get_entities_by_type(ENT_TYPE.MONS_SHOPKEEPER)[1])
get_entity(spawn(ENT_TYPE.ITEM_SHOTGUN, 0, 0, LAYER.FRONT, 0, 0)):trigger_action(shopkeeper)
