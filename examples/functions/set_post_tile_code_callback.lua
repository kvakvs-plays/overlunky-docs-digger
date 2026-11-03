-- Spawn a rock on top of every floor tile after the game has placed it.
set_post_tile_code_callback(function(x, y, layer)
    spawn_entity(ENT_TYPE.ITEM_ROCK, x, y + 1, layer, 0, 0)
end, "floor")
