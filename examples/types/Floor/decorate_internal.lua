-- Re-decorate a freshly spawned floor tile one frame after it was spawned.
local floor = get_entity(spawn_grid_entity(ENT_TYPE.FLOOR_GENERIC, 10, 100, LAYER.FRONT))
set_global_timeout(function() floor:decorate_internal() end, 1)
