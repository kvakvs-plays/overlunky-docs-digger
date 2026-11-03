-- Turn the rightmost room of every row into a solid room.
set_callback(function(ctx)
    for y = 0, state.height - 1 do ctx:set_room_template(state.width - 1, y, LAYER.FRONT, ROOM_TEMPLATE.SOLID) end
end, ON.POST_ROOM_GENERATION)
