-- Get a copy of the destination quad of the big dollar sign (transition screen), moved a bit to the right.
local icon = state.screen_transition.big_dollar_sign
local moved = icon:dest_get_quad():offset(0.1, 0)
print(moved:get_AABB().left)
