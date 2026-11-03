-- Prevent the first player from crouching by clearing the DOWN button from their inputs.
local input = players[1].input
input.buttons_gameplay = clr_mask(input.buttons_gameplay, INPUTS.DOWN)
