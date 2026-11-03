-- Unmark all set-rooms of the level so the level generator treats them as ordinary rooms.
set_callback(function(ctx)
    for x = 0, state.width - 1 do for y = 0, state.height - 1 do ctx:unmark_as_set_room(x, y, LAYER.FRONT) end end
end, ON.POST_ROOM_GENERATION)
