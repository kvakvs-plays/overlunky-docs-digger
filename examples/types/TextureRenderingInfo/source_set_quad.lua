-- Show only the top left quarter of the texture on the big dollar sign of the transition screen.
local icon = state.screen_transition.big_dollar_sign
icon:source_set_quad(Quad.new(AABB.new(0, 0, 0.5, 0.5)))
