-- Check if the first player is inside a shop.
local x, y, layer = get_position(players[1].uid)
print(is_inside_shop_zone(x, y, layer))
