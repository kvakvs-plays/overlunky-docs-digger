-- Check whether some damage flags include whip damage.
local damage_flags = DAMAGE_TYPE.WHIP | DAMAGE_TYPE.SWORD
print(test_mask(damage_flags, DAMAGE_TYPE.WHIP))
