-- Get the bounding box of a quad and print its center.
local quad = Quad.new(AABB.new(-0.5, 0.5, 0.5, -0.5))
print(quad:get_AABB():center())
