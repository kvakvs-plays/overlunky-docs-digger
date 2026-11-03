-- Kill an entity as if the first player did it, leaving its corpse in place.
get_entity(get_entities_by_type(ENT_TYPE.MONS_SNAKE)[1]):kill(false, players[1])
