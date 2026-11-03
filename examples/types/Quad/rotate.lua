-- Rotate a quad by 45 degrees around its own center.
local quad = Quad.new(AABB.new(-0.5, 0.5, 0.5, -0.5))
quad:rotate(math.rad(45), quad:get_AABB():center())
