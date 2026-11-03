-- Make the first player attack the first caveman as if they had hit it.
local victim = get_entity(get_entities_by_type(ENT_TYPE.MONS_CAVEMAN)[1])
players[1]:attack(victim)
