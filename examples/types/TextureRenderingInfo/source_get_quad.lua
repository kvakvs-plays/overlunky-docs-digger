-- Read the source quad of the big dollar sign (transition screen) and print where its texture region starts.
local icon = state.screen_transition.big_dollar_sign
print(icon:source_get_quad():get_AABB().left)
