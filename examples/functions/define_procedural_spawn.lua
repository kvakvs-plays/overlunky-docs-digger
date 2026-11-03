-- Define a procedural spawn for landmines that are valid only on top of a floor tile.
define_procedural_spawn("my_landmine",
    function(x, y, l) spawn_entity_snapped_to_floor(ENT_TYPE.ITEM_LANDMINE, x, y, l) end,
    function(x, y, l) return get_grid_entity_at(x, y - 1, l) ~= -1 end)
