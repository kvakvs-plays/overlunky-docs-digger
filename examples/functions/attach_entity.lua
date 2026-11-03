-- Attach the first rock to the first player so it moves along with them.
attach_entity(players[1].uid, get_entities_by_type(ENT_TYPE.ITEM_ROCK)[1])
