-- Break the solid block below the player, with camera shake.
local block = get_entity(players[1].standing_on_uid)
if block then players[1]:break_block(true, block) end
