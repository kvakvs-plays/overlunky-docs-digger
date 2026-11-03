-- Make the first rock of the level collide with liquids (water and lava).
add_entity_to_liquid_collision(get_entities_by_type(ENT_TYPE.ITEM_ROCK)[1], true)
