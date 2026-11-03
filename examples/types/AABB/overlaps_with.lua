-- Check whether two hitboxes touch each other.
local a, b = AABB:new(0, 2, 2, 0), players[1]:get_hitbox()
print(a:overlaps_with(b))
