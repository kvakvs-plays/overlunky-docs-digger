-- Never spawn arrow traps: an inverse chance of 0 disables the procedural spawn.
set_callback(function(ctx)
    ctx:set_procedural_spawn_chance(PROCEDURAL_CHANCE.ARROWTRAP_CHANCE, 0)
end, ON.POST_ROOM_GENERATION)
