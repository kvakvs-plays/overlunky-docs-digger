-- Get the first player (false: ignore ghosts) and check whether they stand on the ground.
local player = get_player(1, false)
if player then print(player.standing_on_uid ~= -1) end
