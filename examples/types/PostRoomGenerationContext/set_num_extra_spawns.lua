-- Disable a custom extra spawn on every level by setting its number of spawns to zero.
local extra_spawn_id = define_extra_spawn(function() end, default_spawn_is_valid, 1, 0)
set_callback(function(ctx) ctx:set_num_extra_spawns(extra_spawn_id, 0, 0) end, ON.POST_ROOM_GENERATION)
