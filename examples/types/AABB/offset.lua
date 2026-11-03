-- Move a hitbox 2 tiles up, e.g. to build a detection box above an entity.
local box = AABB:new(-0.5, 0.5, 0.5, -0.5)
box:offset(0, 2)
