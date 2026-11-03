-- Pick a random entry from a list.
local list = { ENT_TYPE.MONS_SNAKE, ENT_TYPE.MONS_BAT, ENT_TYPE.MONS_SPIDER }
print(list[prng:random_index(#list, PRNG_CLASS.LEVEL_GEN)])
