-- Light the first player on fire with a one in four chance.
if prng:random_chance(4, PRNG_CLASS.AI) then players[1]:light_on_fire() end
