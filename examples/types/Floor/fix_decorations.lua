-- Rebuild the decorations of every forcefield floor, including its neighbors, as soon as it spawns.
set_post_entity_spawn(function(floor) floor:fix_decorations(true, true) end, SPAWN_TYPE.ANY, 0,
    ENT_TYPE.FLOOR_HORIZONTAL_FORCEFIELD)
