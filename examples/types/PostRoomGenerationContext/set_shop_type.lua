-- Make the shop in room (1, 1) a general store.
set_callback(function(ctx)
    ctx:set_shop_type(1, 1, LAYER.FRONT, SHOP_TYPE.GENERAL_STORE)
end, ON.POST_ROOM_GENERATION)
