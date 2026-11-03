-- Check if the room the first player is in is a dice shop.
local x, y, layer = get_position(players[1].uid)
local room_x, room_y = get_room_index(x, y)
print(get_room_template(room_x, room_y, layer) == ROOM_TEMPLATE.DICESHOP)
