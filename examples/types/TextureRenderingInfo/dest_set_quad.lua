-- Move and resize the big dollar sign of the transition screen to the top left corner.
local icon = state.screen_transition.big_dollar_sign
icon:dest_set_quad(Quad.new(AABB.new(-0.9, 0.9, -0.7, 0.7)))
