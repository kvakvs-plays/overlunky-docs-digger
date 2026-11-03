-- Check if the first player is swimming using the second set of entity flags.
local swimming = test_flag(get_entity_flags2(players[1].uid), ENT_MORE_FLAG.SWIMMING)
print(swimming)
