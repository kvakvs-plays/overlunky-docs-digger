-- Get the box of the whole level, cut it to the top half and look for rocks inside.
local box = get_aabb_bounds()
box.bottom = (box.top + box.bottom) / 2
local rocks = get_entities_overlapping_hitbox(ENT_TYPE.ITEM_ROCK, MASK.ITEM, box, LAYER.FRONT)
print(#rocks)
