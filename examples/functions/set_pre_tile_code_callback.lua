-- Replace the floor tile code with a different floor style and stop the game from spawning its own.
set_pre_tile_code_callback(function(x, y, layer)
    spawn_grid_entity(ENT_TYPE.FLOORSTYLED_GUTS, x, y, layer)
    return true
end, "floor")
