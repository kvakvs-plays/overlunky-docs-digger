-- Make liquids collide with every ice tile, as if solid floor was there.
for _, uid in ipairs(get_entities_by_type(ENT_TYPE.FLOOR_ICE)) do
    local x, y = get_position(uid)
    update_liquid_collision_at(x, y, true)
end
